/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <istream>
#include <ostream>
#include <cstring>
#include "definitions.h"
#include "checksum.hpp"

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
        uint64_t byte_offset;   ///< Absolute file offset to PacketHeader (bytes from file start)
        uint64_t first_row;     ///< First row index in this packet (0-based, file-wide)
        
        PacketIndexEntry() : byte_offset(0), first_row(0) {}
        PacketIndexEntry(uint64_t byteOffset, uint64_t firstRow)
            : byte_offset(byteOffset), first_row(firstRow) {}
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
     * +8+N×16           | 4 bytes   | startOffset (bytes from EOF to "BIDX")
     * +12+N×16          | 8 bytes   | rowCount
     * +20+N×16          | 8 bytes   | checksum (xxHash64 of entire index)
     * ```
     * 
     * Footer is always at fixed offset from EOF: `-24 bytes`
     * 
     * Total size = 28 + N×16 bytes (where N = number of packets)
     * Example: 1000 packets = 16,028 bytes (~16 KB)
     * 
     * Note: start_offset is uint32_t (max ~4GB footer = ~268M packets).
     * This does NOT limit overall file size — PacketIndexEntry::byte_offset
     * uses uint64_t for file-level addressing.
     */

    class FileFooter {
    public: 
        #pragma pack(push, 1)
        struct ConstSection {
            uint32_t start_magic;      ///< "EIDX" magic number
            uint32_t start_offset;     ///< Bytes from EOF to "BIDX" (footer size)
            uint64_t row_count;        ///< Total number of rows in file
            uint64_t checksum;         ///< xxHash64 of entire index (from "BIDX" to totalRowCount)
            
            ConstSection(uint32_t startOffset = 0, uint64_t totalRows = 0)
                : start_magic(FOOTER_EIDX_MAGIC)
                , start_offset(startOffset)
                , row_count(totalRows)
                , checksum(0)
            {}
        };
        #pragma pack(pop)
        static_assert(sizeof(ConstSection) == 24, "FileFooter must be exactly 24 bytes");

        /**
         * @brief Default constructor
         */
        FileFooter(PacketIndex index = PacketIndex(), uint64_t totalRowCount = 0)
            : packet_index_(std::move(index))
            , const_section_(0, totalRowCount)
        {
            const_section_.start_offset = static_cast<uint32_t>(encodedSize());
        }
        
        bool hasValidIndex() const
        {
            // check if packeIndex size matches totalRowCount
            return !packet_index_.empty() && const_section_.row_count > 0;
        }

        /**
         * @brief Get all packet entries (const)
         * @return Const reference to packetIndex
         */
        const auto& packetIndex() const {
            return packet_index_;
        }

        /// @brief Append a packet entry to the index. Preferred over mutating packetIndex() directly.
        void addPacketEntry(uint64_t byteOffset, uint64_t firstRow) {
            packet_index_.emplace_back(byteOffset, firstRow);
        }
                
        /**
         * @brief Access to files total row count
         * @param reference to totalRowCount
         */
        auto& rowCount() {
            return const_section_.row_count;
        }
        
        /**
         * @brief Access to files total row count
         * @param const reference to totalRowCount
         */
        const auto& rowCount() const {
            return const_section_.row_count;
        }
        
        /**
         * @brief Clear all index data
         */
        void clear() {
            packet_index_.clear();
            const_section_.start_offset = static_cast<uint32_t>(encodedSize());
            const_section_.row_count = 0;
            const_section_.checksum = 0;
        }
                
        /**
         * @brief Calculate total size of serialized index
         * @return Size in bytes
         */
        size_t encodedSize() const {
            return  4                                                   // "BIDX" magic
                    + packet_index_.size() * sizeof(PacketIndexEntry)    // Packet entries
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
         * 4. startOffset (4 bytes) - bytes from EOF to "BIDX" (footer size)
         * 5. rowCount (8 bytes) - total number of rows in file
         * 6. checksum (8 bytes) - xxHash64 of bytes 0 to rowCount
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
            const_section_.start_offset = static_cast<uint32_t>(encodedSize());

            // Calculate checksum
            Checksum::Streaming checksum;
            checksum.update(MAGIC_BYTES_FOOTER_BIDX, 4);
            checksum.update(packet_index_.data(), packet_index_.size() * sizeof(PacketIndexEntry));
            checksum.update(&const_section_, sizeof(ConstSection) - sizeof(const_section_.checksum));
            const_section_.checksum = checksum.finalize();

            // Write to stream
            stream.write(MAGIC_BYTES_FOOTER_BIDX, 4);
            stream.write(reinterpret_cast<const char*>(packet_index_.data()), packet_index_.size() * sizeof(PacketIndexEntry));
            stream.write(reinterpret_cast<const char*>(&const_section_), sizeof(ConstSection));
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
            stream.read(reinterpret_cast<char*>(&const_section_), sizeof(ConstSection));
            
            if (!stream.good() || const_section_.start_magic != FOOTER_EIDX_MAGIC) {
                stream.seekg(originalPos);
                return false;
            }

            // Seek to index start
            stream.seekg(-static_cast<int64_t>(const_section_.start_offset), std::ios::end);
            
            // Read start magic
            char startMagic[4];
            stream.read(startMagic, 4);
            if (!stream.good() || std::memcmp(startMagic, "BIDX", 4) != 0) {
                stream.seekg(originalPos);
                return false;
            }

            // Calculate number of packet entries
            size_t indexSize = const_section_.start_offset - sizeof(ConstSection) - sizeof(startMagic);
            if (indexSize % sizeof(PacketIndexEntry) != 0) {
                stream.seekg(originalPos);
                return false;
            }
            size_t entryCount = indexSize / sizeof(PacketIndexEntry);

            // Read packet entries
            packet_index_.resize(entryCount);
            stream.read(reinterpret_cast<char*>(packet_index_.data()), indexSize);
            if (!stream.good()) {
                stream.seekg(originalPos);
                return false;
            }

            // validate checksum
            Checksum::Streaming checksum;
            checksum.update(startMagic, 4);
            checksum.update(packet_index_.data(), indexSize);
            checksum.update(&const_section_, sizeof(ConstSection) - sizeof(const_section_.checksum));
            uint64_t calculatedChecksum = checksum.finalize();
            if (calculatedChecksum != const_section_.checksum) {
                stream.seekg(originalPos);
                return false;
            }
            const_section_.checksum = calculatedChecksum;
            return true;
        }
                
    private:
        PacketIndex   packet_index_;
        ConstSection  const_section_;
    };
    
} // namespace bcsv
