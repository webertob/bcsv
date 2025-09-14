#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

#include "definitions.h"
#include "layout.h"

namespace bcsv {
    // Forward declarations
    class Layout;
    template<typename... ColumnTypes>
    class LayoutStatic;
    enum class ColumnType : uint16_t;

    /**
     * @brief Represents the file header with controlled memory layout for direct I/O
     * 
     * The BCSV file format uses a structured binary layout optimized for efficient parsing
     * and minimal storage overhead. The layout is designed to be platform-independent
     * with explicit byte ordering and fixed-size fields.
     * 
     * @section binary_layout Complete Binary File Layout
     * 
     * The file consists of three main sections:
     * 1. Fixed Header (12 bytes) - Core file metadata
     * 2. Variable Schema (varies) - Column definitions  
     * 3. Data Records (varies) - Actual row data
     * 
     * @subsection fixed_header Fixed Header Section (12 bytes)
     * 
     * ```
     * Offset | Size | Type    | Description
     * -------|------|---------|------------------------------------------
     *   0    |  4   | uint32  | Magic number: 0x56534342 ("BCSV")
     *   4    |  1   | uint8   | Major version number
     *   5    |  1   | uint8   | Minor version number  
     *   6    |  1   | uint8   | Patch version number
     *   7    |  1   | uint8   | Compression level (0-9, 0=none)
     *   8    |  2   | uint16  | Feature flags (bitfield)
     *  10    |  2   | uint16  | Number of columns (N)
     * ```
     * 
     * @subsection variable_schema Variable Schema Section
     * 
     * This section defines the structure of each column and varies in size based
     * on the number of columns and length of column names.
     * 
     * ```
     * Section              | Size        | Description
     * ---------------------|-------------|----------------------------------------
     * Column Data Types    | N * 2 bytes | Type enum for each column (uint16)
     * Column Name Lengths  | N * 2 bytes | Length of each column name (uint16)
     * Column Names         | Variable    | Concatenated column names (no null terminators)
     * ```
     * 
     * @subsection memory_diagram Memory Layout Diagram
     * 
     * Example with 3 columns: "name" (string), "age" (int64), "salary" (double)
     * 
     * ```
     * Byte    0    1    2    3    4    5    6    7    8    9   10   11
     *      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
     *      │    Magic Number   │Maj │Min │Pat │Cmp │  Flags  │ Cols=3  │
     *      │   0x56534342      │ 1  │ 0  │ 0  │ 0  │  0x00   │  0x03   │
     *      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
     * 
     * Byte   12   13   14   15   16   17   18   19   20   21   22   23
     *      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
     *      │  string │  int64  │  double │ len=4   │ len=3   │ len=6   │
     *      │   0x0C  │   0x09  │   0x0B  │ 0x04    │ 0x03    │ 0x06    │
     *      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
     * 
     * Byte   24   25   26   27   28   29   30   31   32   33   34   35   36
     *      ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐
     *      │ n  │ a  │ m  │ e  │ a  │ g  │ e  │ s  │ a  │ l  │ a  │ r  │ y  │
     *      │0x6E│0x61│0x6D│0x65│0x61│0x67│0x65│0x73│0x61│0x6C│0x61│0x72│0x79│
     *      └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
     * ```
     * 
     * Total header size in this example: 37 bytes
     * 
     * @subsection data_types Column Data Type Encoding
     * 
     * Each column type is encoded as a 16-bit unsigned integer:
     * - 0x00: bool
     * - 0x01: uint8
     * - 0x02: uint16
     * - 0x03: uint32
     * - 0x04: uint64
     * - 0x05: int8
     * - 0x06: int16
     * - 0x07: int32
     * - 0x08: int64
     * - 0x09: float
     * - 0x0A: double
     * - 0x0B: string
     * 
     * @subsection flag_bits Feature Flags (16-bit bitfield)
     * - 0x0001: Zero-Order-Hold compression enabled
     * - Bits 1-15: Reserved for future optional features
     * 
     * @note All multi-byte integers are stored in little-endian format
     * @note Column names are stored without null terminators to save space
     * @note The magic number 0x56534342 spells "BCSV" in ASCII when read as bytes
     */
    class FileHeader {
    public:
        /**
         * @brief Fixed-size header structure with controlled memory layout
         * 
         * This structure represents the first 12 bytes of every BCSV file.
         * The #pragma pack directive ensures no padding is inserted between
         * fields, guaranteeing consistent binary layout across platforms.
         * 
         * @note The structure is exactly 12 bytes as verified by static_assert
         * @note All multi-byte fields use little-endian byte ordering
         */
        #pragma pack(push, 1)
        struct FileHeaderStruct {
            uint32_t magic;            ///< Magic number: 0x56534342 ("BCSV" in ASCII)
            uint8_t  versionMajor;     ///< Major version number (0-255)
            uint8_t  versionMinor;     ///< Minor version number (0-255)
            uint8_t  versionPatch;     ///< Patch version number (0-255)
            uint8_t  compressionLevel; ///< Compression level (0=none, 1-9=LZ4 levels)
            uint16_t flags;            ///< Feature flags bitfield
            uint16_t columnCount;      ///< Number of columns in the file
        };
        #pragma pack(pop)

        static_assert(sizeof(FileHeaderStruct) == 12, "BinaryHeader must be exactly 12 bytes");

        /**
         * @brief File format constants and limits
         */
        static constexpr size_t FIXED_HEADER_SIZE  = sizeof(FileHeaderStruct);  ///< Size of fixed header: 12 bytes
        static constexpr size_t COLUMN_TYPE_SIZE   = sizeof(uint16_t);          ///< Size per column type: 2 bytes  
        static constexpr size_t COLUMN_LENGTH_SIZE = sizeof(uint16_t);          ///< Size per name length: 2 bytes
        
        // Constructors
        FileHeader(size_t columnCount = 0, uint8_t compressionLevel = 9, uint8_t major = VERSION_MAJOR, uint8_t minor = VERSION_MAJOR, uint8_t patch = VERSION_PATCH);
        ~FileHeader() = default;

        // Version management
        void        setVersion(uint8_t major, uint8_t minor, uint8_t patch) 
                                                        { header_.versionMajor = major;
                                                          header_.versionMinor = minor;
                                                          header_.versionPatch = patch; }
        std::string getVersionString() const            { return    std::to_string(header_.versionMajor) + "." + 
                                                                    std::to_string(header_.versionMinor) + "." + 
                                                                    std::to_string(header_.versionPatch); }
        uint8_t     getVersionMajor() const             { return header_.versionMajor; }
        uint8_t     getVersionMinor() const             { return header_.versionMinor; }
        uint8_t     getVersionPatch() const             { return header_.versionPatch; }

        // Compression management
        void        setCompressionLevel(size_t level)   { header_.compressionLevel = (level > 9) ? 9 : level; }
        uint8_t     compressionLevel() const            { return header_.compressionLevel; }

        // Flags management
        void        setFlag(uint16_t flag, bool value);
        bool        getFlag(uint16_t flag) const        { return (header_.flags & flag) != 0; }
        uint16_t    getFlags() const                    { return header_.flags; }
        void        setFlags(uint16_t flags)            { header_.flags = flags; }

        // Magic number validation
        bool        isValidMagic() const                { return header_.magic == BCSV_MAGIC; }
        uint32_t    getMagic() const                    { return header_.magic; }

        /**
         * @brief Binary I/O operations for complete file headers
         * 
         * These methods handle serialization and deserialization of the complete
         * file header including both the fixed-size portion and variable-length
         * column schema information.
         */
        
        /**
         * @brief Calculate total binary size of header including column schema
         * @param columnLayout The column layout to include in size calculation
         * @return Total size in bytes needed for complete header
         */
        template<LayoutConcept LayoutType>
        static size_t getBinarySize(const LayoutType& layout);

        /**
         * @brief Read complete header from binary stream
         * @param stream Input stream to read from
         * @param columnLayout Column layout to populate from stream
         * @return true if read was successful, false on error or invalid data
         */
        bool readFromBinary(std::istream& stream, Layout& columnLayout);

        /**
         * @brief Read complete header from binary stream
         * @param stream Input stream to read from
         * @param columnLayout Column layout to populate from stream
         * @return true if read was successful, false on error or invalid data
         */
        template<typename... ColumnTypes>
        bool readFromBinary(std::istream& stream, LayoutStatic<ColumnTypes...>& columnLayout);
        
        /**
         * @brief Write complete header to binary stream
         * @param stream Output stream to write to
         * @param columnLayout Column layout to serialize with header
         * @return true if write was successful, false on error
         */
        template<LayoutConcept LayoutType>
        bool writeToBinary(std::ostream& stream, const LayoutType& columnLayout);

        /**
         * @brief Print detailed binary layout information for debugging
         * @param columnLayout Column layout to analyze
         */
        template<LayoutConcept LayoutType>
        void printBinaryLayout(const LayoutType& columnLayout) const;

    private:
        FileHeaderStruct header_;
    };

} // namespace bcsv
