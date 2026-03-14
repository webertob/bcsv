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
 * BCSV Quick Start
 *
 * Minimal example: write three rows, read them back.
 */

int main() {
    const std::string filename = "quickstart.bcsv";

    // ── Write ──
    bcsv::Layout layout;
    layout.addColumn({"sensor", bcsv::ColumnType::STRING});
    layout.addColumn({"value",  bcsv::ColumnType::DOUBLE});

    bcsv::Writer<bcsv::Layout> writer(layout);
    if (!writer.open(filename, true)) {
        std::cerr << "Write error: " << writer.getErrorMsg() << "\n";
        return 1;
    }

    auto& wrow = writer.row();
    for (int i = 0; i < 3; ++i) {
        wrow.set(0, std::string("temp_" + std::to_string(i)));
        wrow.set(1, 20.0 + i * 0.5);
        writer.writeRow();
    }
    writer.close();
    std::cout << "Wrote 3 rows to " << filename << "\n";

    // ── Read ──
    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Read error: " << reader.getErrorMsg() << "\n";
        return 1;
    }

    while (reader.readNext()) {
        auto& rrow = reader.row();
        std::cout << rrow.get<std::string>(0) << "  " << rrow.get<double>(1) << "\n";
    }
    reader.close();

    std::remove(filename.c_str());
    return 0;
}
