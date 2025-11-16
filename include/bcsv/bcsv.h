/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
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
#include "file_header.h"
#include "layout.h"
#include "packet_header.h"
#include "reader.h"
#include "row.h"
#include "writer.h"

// Include implementations
#include "bcsv.hpp"
#include "file_header.hpp"
#include "layout.hpp"
#include "reader.hpp"
#include "row.hpp"
#include "writer.hpp"
