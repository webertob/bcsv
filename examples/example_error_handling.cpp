/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <iostream>
#include <bcsv/bcsv.h>

/**
 * BCSV Error Handling Example
 *
 * Demonstrates the error handling patterns:
 *  - I/O operations return bool; call getErrorMsg() for details.
 *  - Logic errors (type mismatch, out-of-range) throw exceptions.
 */

int main() {
    const std::string filename = "example_error_handling.bcsv";

    // ── 1. Writer errors ──
    std::cout << "=== Writer error handling ===\n\n";

    bcsv::Layout layout;
    layout.addColumn({"id",    bcsv::ColumnType::INT32});
    layout.addColumn({"value", bcsv::ColumnType::DOUBLE});

    {
        // Write a small file first
        bcsv::Writer<bcsv::Layout> writer(layout);
        if (!writer.open(filename, true)) {
            std::cerr << "  open failed: " << writer.getErrorMsg() << "\n";
            return 1;
        }
        auto& row = writer.row();
        row.set(0, int32_t(1));
        row.set(1, 42.0);
        writer.writeRow();
        writer.close();
        std::cout << "  Wrote 1 row successfully.\n";
    }

    {
        // Try to open again without overwrite — should fail
        bcsv::Writer<bcsv::Layout> writer(layout);
        if (!writer.open(filename, false)) {
            std::cout << "  Expected failure (no overwrite): "
                      << writer.getErrorMsg() << "\n";
        }
    }

    // ── 2. Reader errors ──
    std::cout << "\n=== Reader error handling ===\n\n";

    {
        // Open a non-existent file
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open("no_such_file.bcsv")) {
            std::cout << "  Expected failure (missing file): "
                      << reader.getErrorMsg() << "\n";
        }
    }

    {
        // Successful read + EOF detection
        bcsv::Reader<bcsv::Layout> reader;
        if (!reader.open(filename)) {
            std::cerr << "  open failed: " << reader.getErrorMsg() << "\n";
            return 1;
        }

        size_t rows = 0;
        while (reader.readNext()) {
            ++rows;
        }

        // After loop: readNext() returned false.
        // Empty error message means normal EOF; non-empty means real error.
        if (reader.getErrorMsg().empty()) {
            std::cout << "  Read " << rows << " row(s); reached EOF normally.\n";
        } else {
            std::cerr << "  Read error: " << reader.getErrorMsg() << "\n";
        }
        reader.close();
    }

    // ── 3. Logic errors (exceptions) ──
    std::cout << "\n=== Logic error handling ===\n\n";

    try {
        bcsv::Row row(layout);
        // Column index 99 is out of range — throws
        row.set(99, int32_t(0));
    } catch (const std::exception& e) {
        std::cout << "  Caught exception (out-of-range): " << e.what() << "\n";
    }

    std::cout << "\nAll error scenarios demonstrated.\n";

    std::remove(filename.c_str());
    return 0;
}
