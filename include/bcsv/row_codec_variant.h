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
 * @file row_codec_variant.h
 * @brief RowCodecType — compile-time codec selection for row codecs.
 *
 * Used by Writer and Reader to select the appropriate codec at compile time
 * based on the TrackingPolicy template parameter.  Direct calls allow the
 * compiler to inline serialize/deserialize into the hot loops.
 *
 * @see row_codec_flat001.h, row_codec_zoh001.h
 * @see ITEM_11_PLAN.md §2.1 for architecture.
 */

#include "row_codec_flat001.h"
#include "row_codec_zoh001.h"
#include "definitions.h"

#include <type_traits>

namespace bcsv {

/// Codec type alias — statically selected based on TrackingPolicy.
///
/// When TrackingPolicy::Disabled → RowCodecFlat001  (dense flat encoding).
/// When TrackingPolicy::Enabled  → RowCodecZoH001   (zero-order hold, wraps Flat001).
///
/// No std::variant, no std::visit — the Policy template parameter fully
/// determines the codec type at compile time.  Direct calls allow the
/// compiler to inline serialize/deserialize into the Writer/Reader hot loops.
template<typename LayoutType, TrackingPolicy Policy>
using RowCodecType = std::conditional_t<
    isTrackingEnabled(Policy),
    RowCodecZoH001<LayoutType, Policy>,
    RowCodecFlat001<LayoutType, Policy>
>;

} // namespace bcsv
