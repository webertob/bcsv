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
 * BCSV Delta Encoding Example
 *
 * Demonstrates WriterDelta for time-series data where consecutive rows
 * have small numeric differences. Delta + VLE encoding compresses such
 * data significantly compared to flat or ZoH codecs.
 */

int main() {
    const std::string filename = "example_delta.bcsv";

    // ── Schema: simulated temperature sensor log ──
    bcsv::Layout layout;
    layout.addColumn({"timestamp", bcsv::ColumnType::INT64});
    layout.addColumn({"sensor_id", bcsv::ColumnType::INT32});
    layout.addColumn({"temperature", bcsv::ColumnType::DOUBLE});
    layout.addColumn({"status", bcsv::ColumnType::STRING});

    // ── Write with Delta codec ──
    std::cout << "=== Writing with Delta Encoding ===\n\n";

    bcsv::WriterDelta<bcsv::Layout> writer(layout);
    if (!writer.open(filename, true)) {
        std::cerr << "Write error: " << writer.getErrorMsg() << "\n";
        return 1;
    }

    // Simulate slowly-changing sensor data (ideal for delta encoding)
    auto& wrow = writer.row();
    int64_t ts = 1'700'000'000;
    double temp = 21.3;

    for (int i = 0; i < 20; ++i) {
        wrow.set(0, ts);
        wrow.set(1, int32_t(42));        // constant — compresses to zero delta
        wrow.set(2, temp);
        wrow.set(3, std::string(i % 5 == 0 ? "calibrating" : "ok"));
        writer.writeRow();

        ts += 1000;                       // +1 s — small delta
        temp += 0.05 * ((i % 3) - 1);    // tiny drift
    }

    writer.close();
    std::cout << "Wrote 20 rows to " << filename << "\n\n";

    // ── Read back ──
    std::cout << "=== Reading back ===\n\n";

    bcsv::Reader<bcsv::Layout> reader;
    if (!reader.open(filename)) {
        std::cerr << "Read error: " << reader.getErrorMsg() << "\n";
        return 1;
    }

    std::cout << std::setw(16) << "timestamp"
              << std::setw(12) << "sensor_id"
              << std::setw(14) << "temperature"
              << "  status\n";
    std::cout << std::string(54, '-') << "\n";

    while (reader.readNext()) {
        auto& rrow = reader.row();
        std::cout << std::setw(16) << rrow.get<int64_t>(0)
                  << std::setw(12) << rrow.get<int32_t>(1)
                  << std::setw(14) << std::fixed << std::setprecision(4)
                                   << rrow.get<double>(2)
                  << "  " << rrow.get<std::string>(3) << "\n";
    }
    reader.close();

    std::cout << "\nDelta encoding is most effective when consecutive rows "
                 "have small numeric differences.\n";

    std::remove(filename.c_str());
    return 0;
}
