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
 * @file row_codec_detail.h
 * @brief Internal helpers shared by all RowCodec implementations.
 *
 * Contains wire-format arithmetic utilities used by RowCodecFlat001,
 * RowCodecZoH001, and future codec classes.
 */

#include "definitions.h"
#include <cstdint>
#include <vector>

namespace bcsv::detail {

    /// Compute wire-format section sizes and per-column offsets from a type list.
    /// @param types      Column type vector
    /// @param offsets    [out] per-column section-relative offsets (bool→bit index, scalar→byte offset, string→string index)
    /// @param bitsSize   [out] ⌈bool_count / 8⌉
    /// @param dataSize   [out] sum of sizeOf(type) for scalar columns
    /// @param strgCount  [out] number of string columns
    inline void computeWireMetadata(
        const std::vector<ColumnType>& types,
        std::vector<uint32_t>& offsets,
        uint32_t& bitsSize,
        uint32_t& dataSize,
        uint32_t& strgCount)
    {
        const size_t n = types.size();
        offsets.resize(n);
        size_t boolIdx = 0, dataOff = 0, strgIdx = 0;
        for (size_t i = 0; i < n; ++i) {
            const ColumnType type = types[i];
            if (type == ColumnType::BOOL) {
                offsets[i] = static_cast<uint32_t>(boolIdx++);
            } else if (type == ColumnType::STRING) {
                offsets[i] = static_cast<uint32_t>(strgIdx++);
            } else {
                offsets[i] = static_cast<uint32_t>(dataOff);
                dataOff += sizeOf(type);
            }
        }
        bitsSize  = static_cast<uint32_t>((boolIdx + 7) / 8);
        dataSize  = static_cast<uint32_t>(dataOff);
        strgCount = static_cast<uint32_t>(strgIdx);
    }

} // namespace bcsv::detail
