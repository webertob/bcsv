#pragma once

/**
 * @file bcsv.h
 * @brief Binary CSV (BCSV) Library - Main Header with Declarations
 * 
 * A C++17 header-only library for reading and writing binary CSV files
 * with type safety, compression support, and efficient I/O operations.
 * 
 * This header includes all BCSV component declarations:
 * - FileHeader: Binary file header management
 * - ColumnLayout: Column definitions and metadata
 * - Row: Individual data rows
 * - Packet: Data packet abstraction
 * - Reader: Template-based file reading
 * - Writer: Template-based file writing
 */

#include <iostream>
#include <string>

// Core definitions first
#include "definitions.h"

// Core component declarations
#include "column_layout.h"
#include "compression.h"
#include "file_header.h"
// #include "packet.h"      // Using packet_header.h instead
#include "packet_header.h"
#include "reader.h"
#include "row.h"
#include "writer.h"

// Include implementations
#include "bcsv.hpp"
#include "column_layout.hpp"
#include "compression.hpp"
#include "file_header.hpp"
#include "packet_header.hpp"
#include "reader.hpp"
#include "row.hpp"
#include "writer.hpp"
