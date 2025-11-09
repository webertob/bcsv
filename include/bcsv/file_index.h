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
#include "byte_buffer.h"

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
        uint64_t headerOffset;      ///< Absolute file offset to PacketHeader (bytes from file start)
        uint64_t firstRowIndex;     ///< First row index in this packet (0-based, file-wide)
        
        PacketIndexEntry() : headerOffset(0), firstRowIndex(0) {}
        PacketIndexEntry(uint64_t offset, uint64_t rowIndex)
            : headerOffset(offset), firstRowIndex(rowIndex) {}
    };
    #pragma pack(pop)
    
    static_assert(sizeof(PacketIndexEntry) == 16, "PacketIndexEntry must be exactly 16 bytes");
    
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
    class FileIndex {
    public:
        static constexpr size_t FOOTER_SIZE = 32; ///< Footer size in bytes (EIDX + 4 fields)
        
        /**
         * @brief Default constructor
         */
        FileIndex()
            : lastPacketPayloadChecksum_(0)
            , totalRowCount_(0)
        {}
        
        /**
         * @brief Add a packet index entry
         * @param headerOffset Absolute file offset to packet header
         * @param firstRowIndex First row index in the packet
         */
        void addPacket(uint64_t headerOffset, uint64_t firstRowIndex) {
            packets_.emplace_back(headerOffset, firstRowIndex);
        }
        
        /**
         * @brief Get the number of packets
         * @return Number of packet index entries
         */
        size_t packetCount() const {
            return packets_.size();
        }
        
        /**
         * @brief Get a packet index entry
         * @param index Packet index (0-based)
         * @return Const reference to PacketIndexEntry
         */
        const PacketIndexEntry& getPacket(size_t index) const {
            return packets_.at(index);
        }
        
        /**
         * @brief Get all packet entries
         * @return Const reference to packet vector
         */
        const auto& getPackets() const {
            return packets_;
        }
        
        /**
         * @brief Set the last packet payload checksum
         * @param checksum xxHash64 of last packet's payload
         */
        void setLastPacketPayloadChecksum(uint64_t checksum) {
            lastPacketPayloadChecksum_ = checksum;
        }
        
        /**
         * @brief Get the last packet payload checksum
         * @return xxHash64 of last packet's payload
         */
        uint64_t getLastPacketPayloadChecksum() const {
            return lastPacketPayloadChecksum_;
        }
        
        /**
         * @brief Set the total row count
         * @param count Total number of rows in file
         */
        void setTotalRowCount(uint64_t count) {
            totalRowCount_ = count;
        }
        
        /**
         * @brief Get the total row count
         * @return Total number of rows in file
         */
        uint64_t getTotalRowCount() const {
            return totalRowCount_;
        }
        
        /**
         * @brief Clear all index data
         */
        void clear() {
            packets_.clear();
            lastPacketPayloadChecksum_ = 0;
            totalRowCount_ = 0;
        }
        
        /**
         * @brief Calculate total size of serialized index
         * @return Size in bytes
         */
        size_t calculateSize() const {
            return 4                              // "BIDX" magic
                 + packets_.size() * 16           // Packet entries
                 + 4                              // "EIDX" magic
                 + 4                              // indexStartOffset
                 + 8                              // lastPacketPayloadChecksum
                 + 8                              // totalRowCount
                 + 8;                             // indexChecksum
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
            // Build the index data in memory for checksum calculation
            std::vector<uint8_t> indexData;
            size_t dataSize = calculateSize() - 8; // All except the final checksum
            indexData.reserve(dataSize);
            
            // Helper lambda to append data
            auto appendData = [&indexData](const void* data, size_t size) {
                const uint8_t* bytes = static_cast<const uint8_t*>(data);
                indexData.insert(indexData.end(), bytes, bytes + size);
            };
            
            // Build index data
            const char startMagic[4] = {'B', 'I', 'D', 'X'};
            appendData(startMagic, 4);
            
            for (const auto& packet : packets_) {
                appendData(&packet.headerOffset, 8);
                appendData(&packet.firstRowIndex, 8);
            }
            
            const char endMagic[4] = {'E', 'I', 'D', 'X'};
            appendData(endMagic, 4);
            
            uint32_t indexSize = static_cast<uint32_t>(calculateSize());
            appendData(&indexSize, 4);
            
            appendData(&lastPacketPayloadChecksum_, 8);
            appendData(&totalRowCount_, 8);
            
            // Calculate checksum
            uint64_t indexChecksum = XXH64(indexData.data(), indexData.size(), 0);
            
            // Write everything to stream
            stream.write(reinterpret_cast<const char*>(indexData.data()), indexData.size());
            stream.write(reinterpret_cast<const char*>(&indexChecksum), 8);
            
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
            clear();
            
            struct IndexFooter {
                char endMagic[4];
                uint32_t indexStartOffset;
                uint64_t lastPacketPayloadChecksum;
                uint64_t totalRowCount;
                uint64_t indexChecksum;
            };

            IndexFooter footer;
            stream.read(reinterpret_cast<char*>(&footer), sizeof(footer));
            if (!stream.good() || std::memcmp(footer.endMagic, "EIDX", 4) != 0) {
                std::cerr << "Error: Invalid file index footer" << std::endl;
                return false;
            }

            // Seek to index start
            stream.seekg(-static_cast<int64_t>(footer.indexStartOffset), std::ios::end);
            std::streampos indexStart = stream.tellg();
            
            // Read start magic
            char startMagic[4];
            stream.read(startMagic, 4);
            if (!stream.good() || std::memcmp(startMagic, "BIDX", 4) != 0) {
                std::cerr << "Error: Invalid file index start magic" << std::endl;
                return false;
            }
            
            // Calculate number of packet entries
            size_t indexSize = footer.indexStartOffset - sizeof(footer) - sizeof(startMagic);
            if (indexSize % sizeof(PacketIndexEntry) != 0) {
                std::cerr << "Error: Invalid file index size" << std::endl;
                return false;
            }
            size_t entryCount = indexSize / sizeof(PacketIndexEntry);
            
            // Read packet entries
            packets_.resize(entryCount);
            stream.read(reinterpret_cast<char*>(packets_.data()), indexSize);
            if (!stream.good()) {
                std::cerr << "Error: Failed to read packet index entries" << std::endl;
                return false;
            }

            // validate checksum
            Checksum::Streaming checksum;
            checksum.update(startMagic, 4);
            checksum.update(packets_.data(), indexSize);
            checksum.update(&footer, sizeof(footer) - sizeof(footer.indexChecksum));
            uint64_t calculatedChecksum = checksum.finalize();
            if (calculatedChecksum != footer.indexChecksum) {
                std::cerr << "Error: File index checksum mismatch" << std::endl;
                return false;
            }
            
            lastPacketPayloadChecksum_ = footer.lastPacketPayloadChecksum;
            totalRowCount_ = footer.totalRowCount;
            return true;
        }
        
        /**
         * @brief Check if file has a valid index
         * 
         * Positions stream at -28 bytes from EOF and checks for "EIDX" magic.
         * Resets stream position after check.
         * 
         * @param stream Input stream
         * @return true if valid index footer found
         */
        static bool hasValidIndex(std::istream& stream) {
            std::streampos originalPos = stream.tellg();
            
            stream.seekg(-static_cast<int64_t>(FOOTER_SIZE), std::ios::end);
            char magic[4];
            stream.read(magic, 4);
            
            bool valid = stream.good() && std::memcmp(magic, "EIDX", 4) == 0;
            
            stream.seekg(originalPos);
            return valid;
        }
        
    private:
        std::vector<PacketIndexEntry, bcsv::LazyAllocator<PacketIndexEntry> > packets_;
        uint64_t lastPacketPayloadChecksum_;
        uint64_t totalRowCount_;
    };
    
} // namespace bcsv
