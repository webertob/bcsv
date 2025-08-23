#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

#include "definitions.h"

namespace bcsv {
    // Forward declarations
    class ColumnLayout;
    enum class ColumnDataType : uint16_t;

    /**
     * @brief Represents the file header with controlled memory layout for direct I/O
     * 
     * Binary layout:
     * - 4 bytes: Magic number (0x56534342 = "BCSV")
     * - 1 byte:  Major version
     * - 1 byte:  Minor version  
     * - 1 byte:  Patch version
     * - 1 byte:  Compression level (0-9, 0=none)
     * - 2 bytes: Flags/options
     * - 2 bytes: Column count
     * - N*2 bytes: Column data types (2 bytes per column)
     * - N*2 bytes: Column name lengths (2 bytes per column)
     * - Variable: Column names (no null terminator, length specified above)
     */
    class FileHeader {
    public:
        // Fixed-size header structure (packed to control layout)
        #pragma pack(push, 1)
        struct FixedSizeStruct {
            uint32_t magic;          // Magic number: 0x56534342 ("BCSV")
            uint8_t versionMajor;    // Major version
            uint8_t versionMinor;    // Minor version
            uint8_t versionPatch;    // Patch version
            uint8_t compressionLevel; // Compression level (0-9, 0=none)
            uint16_t flags;          // Feature flags
            uint16_t columnCount;    // Number of columns
        };
        #pragma pack(pop)

        static_assert(sizeof(FixedSizeStruct) == 12, "BinaryHeader must be exactly 12 bytes");

        FileHeader();
        explicit FileHeader(uint8_t major, uint8_t minor = 0, uint8_t patch = 0);

        // Version management
        void setVersion(uint8_t major, uint8_t minor, uint8_t patch);
        std::string getVersionString() const;
        uint8_t getVersionMajor() const;
        uint8_t getVersionMinor() const;
        uint8_t getVersionPatch() const;

        // Compression management
        void setCompressionLevel(uint8_t level);
        uint8_t getCompressionLevel() const;
        bool isCompressed() const;

        // Flags management
        void setFlag(uint16_t flag, bool value);
        bool getFlag(uint16_t flag) const;
        uint16_t getFlags() const;
        void setFlags(uint16_t flags);

        // Magic number validation
        bool isValidMagic() const;
        uint32_t getMagic() const;

        // Binary I/O support
        size_t getBinarySize(const ColumnLayout& columnLayout) const;
        bool writeToBinary(std::ostream& stream, const ColumnLayout& columnLayout) const;
        bool readFromBinary(std::istream& stream, ColumnLayout& columnLayout);

        // Debug/utility functions
        void printBinaryLayout(const ColumnLayout& columnLayout) const;

    private:
        FixedSizeStruct header_;
    };

} // namespace bcsv
