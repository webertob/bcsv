/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <bcsv/bcsv.h>

/**
 * BCSV Direct Access Example
 *
 * Demonstrates ReaderDirectAccess for O(log P) random row access
 * (P = number of packets). Useful for large files where you need
 * specific rows without sequential scanning.
 */

int main() {
    const std::string filename = "example_direct_access.bcsv";

    // ── Write a packet-based file with 100 rows ──
    bcsv::Layout layout;
    layout.addColumn({"row_id", bcsv::ColumnType::INT32});
    layout.addColumn({"label",  bcsv::ColumnType::STRING});
    layout.addColumn({"value",  bcsv::ColumnType::DOUBLE});

    bcsv::Writer<bcsv::Layout> writer(layout);
    // Use small block size (64 KB) to create multiple packets
    if (!writer.open(filename, true, 1, 64, bcsv::FileFlags::NONE)) {
        std::cerr << "Write error: " << writer.getErrorMsg() << "\n";
        return 1;
    }

    auto& wrow = writer.row();
    for (int i = 0; i < 100; ++i) {
        wrow.set(0, int32_t(i));
        wrow.set(1, std::string("item_" + std::to_string(i)));
        wrow.set(2, i * 1.1);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Wrote 100 rows to " << filename << "\n\n";

    // ── Open with ReaderDirectAccess ──
    bcsv::ReaderDirectAccess<bcsv::Layout> da;
    if (!da.open(filename)) {
        std::cerr << "Read error: " << da.getErrorMsg() << "\n";
        return 1;
    }

    std::cout << "Total rows in file: " << da.rowCount() << "\n";
    std::cout << "Total packets:      " << da.fileFooter().packetIndex().size() << "\n\n";

    // ── Random access: read specific rows ──
    std::cout << "=== Random Access ===\n\n";
    std::cout << std::setw(8) << "row_id"
              << std::setw(14) << "label"
              << std::setw(12) << "value" << "\n";
    std::cout << std::string(34, '-') << "\n";

    for (size_t idx : {0u, 49u, 99u, 25u, 75u}) {
        if (!da.read(idx)) {
            std::cerr << "Error reading row " << idx << ": "
                      << da.getErrorMsg() << "\n";
            continue;
        }
        auto& rrow = da.row();
        std::cout << std::setw(8)  << rrow.get<int32_t>(0)
                  << std::setw(14) << rrow.get<std::string>(1)
                  << std::setw(12) << std::fixed << std::setprecision(1)
                                   << rrow.get<double>(2) << "\n";
    }

    // ── Sequential scan also works ──
    std::cout << "\n=== Sequential Scan (first 5 rows) ===\n\n";
    da.close();
    da.open(filename);

    int count = 0;
    while (da.readNext() && count < 5) {
        auto& rrow = da.row();
        std::cout << "  row " << rrow.get<int32_t>(0)
                  << ": " << rrow.get<std::string>(1) << "\n";
        ++count;
    }
    da.close();

    std::remove(filename.c_str());
    return 0;
}
