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
#include <vector>
#include <iostream>
#include <cstring>
#include "checksum.hpp"
#include "definitions.h"

namespace bcsv {

    /**
     * @brief Index entry for a single packet in the file
     * 
     * Each packet in the file has a corresponding index entry that stores:
     * - Absolute file offset to the packet header
     * - First row index in the packet (0-based, file-wide)
     * 
     * This enables:
     * - Random access to any packet
     * - Binary search by row index
     * - Fast row count calculation (nextPacket.firstRowIndex - currentPacket.firstRowIndex)
     */
    #pragma pack(push, 1)
    struct PacketIndexEntry {
        uint64_t byteOffset_;   ///< Absolute file offset to PacketHeader (bytes from file start)
        uint64_t firstRow_;     ///< First row index in this packet (0-based, file-wide)
        
        PacketIndexEntry() : byteOffset_(0), firstRow_(0) {}
        PacketIndexEntry(uint64_t byteOffset, uint64_t firstRow)
            : byteOffset_(byteOffset), firstRow_(firstRow) {}
    };
    #pragma pack(pop)
    static_assert(sizeof(PacketIndexEntry) == 16, "PacketIndexEntry must be exactly 16 bytes");
    using PacketIndex = std::vector<PacketIndexEntry>;

    /**
     * @brief File index structure for BCSV v1.3.0 streaming format
     * 
     * The file index is written at EOF when the file is closed. It provides:
     * - Fast random access to any packet
     * - Instant row count lookup
     * - Last packet payload checksum validation
     * - Integrity validation via index checksum
     * 
     * File Index Layout:
     * ```
     * Offset            | Size      | Field
     * ------------------|-----------|------------------------------------------
     * indexStartOffset  | 4 bytes   | Magic: "BIDX"
     * +4                | N×16 bytes| Packet index entries (headerOffset, firstRowIndex)
     * +4+N×16           | 4 bytes   | Magic: "EIDX"
     * +8+N×16           | 4 bytes   | indexStartOffset (bytes from EOF to "BIDX")
     * +12+N×16          | 8 bytes   | lastPacketPayloadChecksum (xxHash64)
     * +20+N×16          | 8 bytes   | totalRowCount
     * +28+N×16          | 8 bytes   | indexChecksum (xxHash64 of entire index)
     * ```
     * 
     * Footer is always at fixed offset from EOF: `-28 bytes`
     * 
     * Total size = 36 + N×16 bytes (where N = number of packets)
     * Example: 1000 packets = 16,036 bytes (16 KB)
     */

    class FileFooter {
    public: 
        #pragma pack(push, 1)
        struct ConstSection {
            const char startMagic[4];           ///< "EIDX" magic number
            uint32_t indexStartOffset;          ///< Bytes from EOF to "BIDX"
            uint64_t lastPacketPayloadChecksum; ///< xxHash64 of last packet's payload
            uint64_t totalRowCount;             ///< Total number of rows in file
            uint64_t footerChecksum;            ///< xxHash64 of entire index (from "BIDX" to totalRowCount)
            
            ConstSection(uint32_t startOffset = 0, uint64_t lastPacketChecksum = 0, uint64_t totalRows = 0)
                : startMagic{'E', 'I', 'D', 'X'}
                , indexStartOffset(startOffset)
                , lastPacketPayloadChecksum(lastPacketChecksum)
                , totalRowCount(totalRows)
                , footerChecksum(0)
            {}
        };
        #pragma pack(pop)
        static_assert(sizeof(ConstSection) == 32, "FileFooter must be exactly 32 bytes");

        /**
         * @brief Default constructor
         */
        FileFooter(PacketIndex index = PacketIndex(), uint64_t lastPacketChecksum = 0, uint64_t totalRowCount = 0)
            : packetIndex_(std::move(index))
            , constSection_(0, lastPacketChecksum, totalRowCount)
        {
            constSection_.indexStartOffset = static_cast<uint32_t>(encodedSize());
        }
        
        bool hasValidIndex() const
        {
            // check if packeIndex size matches totalRowCount
            return !packetIndex_.empty() && constSection_.totalRowCount > 0;
        }

        /**
         * @brief Get all packet entries
         * @return reference to packetIndex
         */
        auto& packetIndex() {
            return packetIndex_;
        }

        /**
         * @brief Get all packet entries
         * @return Const reference to packetIndex
         */
        const auto& packetIndex() const {
            return packetIndex_;
        }
        
        /**
         * @brief Access to checksum of last packet's payload
         * @param reference to lastPacketPayloadChecksum
         */
        auto& lastPacketPayloadChecksum() {
            return constSection_.lastPacketPayloadChecksum;
        }
        
        /**
         * @brief Access to checksum of last packet's payload
         * @param reference to lastPacketPayloadChecksum
         */
        const auto& lastPacketPayloadChecksum() const {
            return constSection_.lastPacketPayloadChecksum;
        }
        
        /**
         * @brief Access to files total row count
         * @param reference to totalRowCount
         */
        auto& totalRowCount() {
            return constSection_.totalRowCount;
        }
        
        /**
         * @brief Access to files total row count
         * @param const reference to totalRowCount
         */
        const auto& totalRowCount() const {
            return constSection_.totalRowCount;
        }
        
        /**
         * @brief Clear all index data
         */
        void clear() {
            packetIndex_.clear();
            constSection_.indexStartOffset = encodedSize();
            constSection_.lastPacketPayloadChecksum = 0;
            constSection_.totalRowCount = 0;
            constSection_.footerChecksum = 0;
        }
        
        
        /**
         * @brief Calculate total size of serialized index
         * @return Size in bytes
         */
        size_t encodedSize() {
            return  4                                                   // "BIDX" magic
                    + packetIndex_.size() * sizeof(PacketIndexEntry)    // Packet entries
                    + sizeof(ConstSection);                             // Footer
        }
        
        /**
         * @brief Write file index to stream
         * 
         * Writes the complete index structure with checksum validation.
         * Format:
         * 1. "BIDX" magic (4 bytes)
         * 2. Packet entries (N × 16 bytes)
         * 3. "EIDX" magic (4 bytes)
         * 4. indexStartOffset (4 bytes) - bytes from EOF to "BIDX"
         * 5. lastPacketPayloadChecksum (8 bytes)
         * 6. totalRowCount (8 bytes)
         * 7. indexChecksum (8 bytes) - xxHash64 of bytes 0 to totalRowCount
         * 
         * @param stream Output stream to write to
         * @return true if write successful
         */
        bool write(std::ostream& stream) {
            // Checkstream state
            if (!stream.good()) {
                return false;
            }
            // Update indexStartOffset  
            constSection_.indexStartOffset = static_cast<uint32_t>(encodedSize());

            // Calculate checksum
            Checksum::Streaming checksum;
            const char startMagic[4] = {'B', 'I', 'D', 'X'};
            checksum.update(startMagic, 4);
            checksum.update(packetIndex_.data(), packetIndex_.size() * sizeof(PacketIndexEntry));
            checksum.update(&constSection_, sizeof(ConstSection) - sizeof(constSection_.footerChecksum));
            constSection_.footerChecksum = checksum.finalize();

            // Write to stream
            stream.write(startMagic, 4);
            stream.write(reinterpret_cast<const char*>(packetIndex_.data()), packetIndex_.size() * sizeof(PacketIndexEntry));
            stream.write(reinterpret_cast<const char*>(&constSection_), sizeof(ConstSection));
            return stream.good();
        }
        
        /**
         * @brief Read file index from stream
         * 
         * Reads and validates the complete index structure.
         * 
         * @param stream Input stream positioned at footer (-28 bytes from EOF)
         * @return true if read successful and checksums valid
         */
        bool read(std::istream& stream) {
            // Check stream state
            if (!stream.good()) {
                return false;
            }

            // store current position
            std::streampos originalPos = stream.tellg();

            // Seek to footer position
            stream.seekg(-static_cast<std::streamoff>(sizeof(ConstSection)), std::ios::end);

            // read FileFooter's ConstSection
            stream.read(reinterpret_cast<char*>(&constSection_), sizeof(ConstSection));
            
            if (!stream.good() || std::memcmp(constSection_.startMagic, "EIDX", 4) != 0) {
                std::cerr << "Error: Invalid file index footer" << std::endl;
                stream.seekg(originalPos);
                return false;
            }

            // Seek to index start
            stream.seekg(-static_cast<int64_t>(constSection_.indexStartOffset), std::ios::end);
            
            // Read start magic
            char startMagic[4];
            stream.read(startMagic, 4);
            if (!stream.good() || std::memcmp(startMagic, "BIDX", 4) != 0) {
                std::cerr << "Error: Invalid file index start magic" << std::endl;
                stream.seekg(originalPos);
                return false;
            }

            // Calculate number of packet entries
            size_t indexSize = constSection_.indexStartOffset - sizeof(ConstSection) - sizeof(startMagic);
            if (indexSize % sizeof(PacketIndexEntry) != 0) {
                std::cerr << "Error: Invalid file index size" << std::endl;
                stream.seekg(originalPos);
                return false;
            }
            size_t entryCount = indexSize / sizeof(PacketIndexEntry);

            // Read packet entries
            packetIndex_.resize(entryCount);
            stream.read(reinterpret_cast<char*>(packetIndex_.data()), indexSize);
            if (!stream.good()) {
                std::cerr << "Error: Failed to read packet index entries" << std::endl;
                stream.seekg(originalPos);
                return false;
            }

            // validate checksum
            Checksum::Streaming checksum;
            checksum.update(startMagic, 4);
            checksum.update(packetIndex_.data(), indexSize);
            checksum.update(&constSection_, sizeof(ConstSection) - sizeof(constSection_.footerChecksum));
            uint64_t calculatedChecksum = checksum.finalize();
            if (calculatedChecksum != constSection_.footerChecksum) {
                std::cerr << "Error: File index checksum mismatch" << std::endl;
                stream.seekg(originalPos);
                return false;
            }
            constSection_.footerChecksum = calculatedChecksum;
            return true;
        }
                
    private:
        PacketIndex   packetIndex_;
        ConstSection  constSection_;
    };
    
} // namespace bcsv
