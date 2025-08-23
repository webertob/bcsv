#pragma once

/* This file holds all constants and definitions used throughout the BCSV library */
#include <cstdint>
#include <string>
#include <cstddef>

namespace bcsv {

    // Version information
    constexpr int VERSION_MAJOR = 1;
    constexpr int VERSION_MINOR = 0;
    constexpr int VERSION_PATCH = 0;
    
    inline std::string getVersion() {
        return std::to_string(VERSION_MAJOR) + "." + 
               std::to_string(VERSION_MINOR) + "." + 
               std::to_string(VERSION_PATCH);
    }

    // Constants for the binary file format
    constexpr uint32_t BCSV_MAGIC = 0x56534342; // "BCSV" in little-endian
    constexpr size_t MAX_COLUMN_WIDTH = 65535;  // Maximum width of column content
    constexpr size_t MAX_COLUMN_COUNT = 65535;

    // File header flags
    namespace FileFlags {
        constexpr uint16_t COMPRESSED = 0x0001;
        constexpr uint16_t CHECKSUMS = 0x0002;
        constexpr uint16_t ALIGNED = 0x0004;
        constexpr uint16_t ROW_INDEX = 0x0008;
        constexpr uint16_t RESERVED2 = 0x0010;
        constexpr uint16_t RESERVED3 = 0x0020;
        constexpr uint16_t RESERVED4 = 0x0040;
        constexpr uint16_t RESERVED5 = 0x0080;
    }
}