/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

/**
 * @file bcsv.h
 * @brief Binary CSV (BCSV) Library - Main Header with Declarations
 * 
 * A C++20 header-only library for reading and writing binary CSV files
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
#include "file_header.h"
#include "layout.h"
#include "layout_guard.h"
#include "packet_header.h"
#include "reader.h"
#include "row.h"
#include "row_codec_flat001.h"  // Flat001 codec (Item 11)
#include "row_codec_zoh001.h"   // ZoH001 codec (Item 11)
#include "row_codec_dispatch.h" // Runtime dispatch (Item 11)
#include "file_codec_concept.h"      // FileCodec concept + sentinels (Item 12)
#include "file_codec_stream001.h"    // Stream-raw file codec (Item 12)
#include "file_codec_stream_lz4_001.h" // Stream-LZ4 file codec (Item 12)
#include "file_codec_packet001.h"    // Packet-raw file codec (Item 12)
#include "file_codec_packet_lz4_001.h" // Packet-LZ4 file codec (Item 12)
#ifdef BCSV_HAS_BATCH_CODEC
#include "file_codec_packet_lz4_batch001.h" // Batch-LZ4 async file codec
#endif
#include "file_codec_dispatch.h"     // FileCodec runtime dispatch (Item 12)
#include "row_visitors.h"  // Row visitor pattern concepts and helpers
#include "writer.h"

// Include implementations
#include "bcsv.hpp"
#include "column_name_index.hpp"
#include "file_header.hpp"
#include "layout.hpp"
#include "reader.hpp"
#include "row.hpp"
#include "row_codec_flat001.hpp"  // Flat001 codec implementation (Item 11)
#include "row_codec_zoh001.hpp"  // ZoH001 codec implementation (Item 11)
#include "writer.hpp"
