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
 * @file bench_static_layouts.hpp
 * @brief Shared LayoutStatic type definitions and dispatch for benchmark executables
 *
 * Both bench_macro_datasets and bench_codec_compare need to instantiate
 * Writer/Reader with LayoutStatic types.  This header is the single source
 * of truth for the type aliases and the name→type dispatch function.
 */

#include "bench_datasets.hpp"
#include <bcsv/bcsv.h>

#include <string_view>
#include <type_traits>

namespace bench_static {

// ============================================================================
// Type-list metaprogramming helpers
// ============================================================================

template<typename... Ts>
struct TypeList {};

template<typename... Lists>
struct TypeListConcat;

template<>
struct TypeListConcat<> { using type = TypeList<>; };

template<typename... Ts>
struct TypeListConcat<TypeList<Ts...>> { using type = TypeList<Ts...>; };

template<typename... A, typename... B, typename... Rest>
struct TypeListConcat<TypeList<A...>, TypeList<B...>, Rest...> {
    using type = typename TypeListConcat<TypeList<A..., B...>, Rest...>::type;
};

template<typename T, typename Seq>
struct RepeatAsTypeListImpl;

template<typename T, size_t... I>
struct RepeatAsTypeListImpl<T, std::index_sequence<I...>> {
    using type = TypeList<std::conditional_t<true, T, std::integral_constant<size_t, I>>...>;
};

template<typename T, size_t N>
using RepeatAsTypeList = typename RepeatAsTypeListImpl<T, std::make_index_sequence<N>>::type;

template<typename TL>
struct LayoutFromTypeList;

template<typename... Ts>
struct LayoutFromTypeList<TypeList<Ts...>> {
    using type = bcsv::LayoutStatic<Ts...>;
};

template<typename... Lists>
using ConcatTypeLists = typename TypeListConcat<Lists...>::type;

template<typename TL>
using LayoutFromTypeList_t = typename LayoutFromTypeList<TL>::type;

// ============================================================================
// LayoutStatic type aliases for all supported profiles
// ============================================================================

using MixedGenericLayoutStatic = bcsv::LayoutStatic<
    bool, bool, bool, bool, bool, bool,
    int8_t, int8_t, int8_t, int8_t, int8_t, int8_t,
    int16_t, int16_t, int16_t, int16_t, int16_t, int16_t,
    int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
    int64_t, int64_t, int64_t, int64_t, int64_t, int64_t,
    uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t,
    uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t,
    uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
    uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
    float, float, float, float, float, float,
    double, double, double, double, double, double,
    std::string, std::string, std::string, std::string, std::string, std::string
>;

using SparseEventsLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    RepeatAsTypeList<bool, 20>, RepeatAsTypeList<int32_t, 30>, RepeatAsTypeList<float, 20>,
    RepeatAsTypeList<double, 20>, RepeatAsTypeList<std::string, 10>
>>;

using SensorNoisyLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint32_t>, RepeatAsTypeList<float, 24>, RepeatAsTypeList<double, 24>
>>;

using StringHeavyLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<int32_t, int32_t, int32_t, float, float, float, double, double, uint64_t, uint64_t>,
    RepeatAsTypeList<std::string, 20>
>>;

using SimulationSmoothLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, double, uint32_t, bool>, RepeatAsTypeList<float, 48>, RepeatAsTypeList<double, 48>
>>;

using WeatherTimeseriesLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, std::string, std::string, uint8_t>, RepeatAsTypeList<float, 10>, RepeatAsTypeList<float, 6>,
    TypeList<float, uint16_t, float, uint16_t, float, uint16_t, float, uint16_t>,
    RepeatAsTypeList<double, 4>, TypeList<float, float, bool, uint8_t>
>>;

using HighCardinalityStringLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint32_t>, RepeatAsTypeList<std::string, 48>
>>;

using EventLogLayoutStatic = bcsv::LayoutStatic<
    uint64_t, uint64_t,
    std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string,
    float, uint32_t, uint16_t,
    bool, bool,
    double, double, double, double, double, double, double, double,
    uint32_t, uint32_t, uint32_t, uint32_t
>;

using IotFleetLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint64_t,
             std::string, std::string, std::string, std::string, std::string, std::string,
             double, float, float,
             uint8_t, int8_t, uint32_t, uint64_t,
             bool, bool>,
    RepeatAsTypeList<float, 8>
>>;

using FinancialOrdersLayoutStatic = bcsv::LayoutStatic<
    uint64_t, uint64_t,
    std::string, std::string, std::string, std::string, std::string, std::string, std::string, std::string,
    double, uint32_t, double, uint32_t, float,
    bool, bool, bool,
    double, double, float,
    uint64_t
>;

using RealisticMeasurementLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    TypeList<uint64_t, uint64_t, std::string, std::string, std::string, uint8_t>,
    RepeatAsTypeList<float, 8>, RepeatAsTypeList<double, 8>, RepeatAsTypeList<int32_t, 8>,
    RepeatAsTypeList<bool, 4>, TypeList<uint32_t, uint32_t, uint32_t, uint32_t>
>>;

using MeasurementCampaignLayoutStatic = LayoutFromTypeList_t<ConcatTypeLists<
    RepeatAsTypeList<bool, 6>,
    RepeatAsTypeList<int8_t, 6>,
    RepeatAsTypeList<int16_t, 6>,
    RepeatAsTypeList<int32_t, 6>,
    RepeatAsTypeList<int64_t, 6>,
    RepeatAsTypeList<uint8_t, 6>,
    RepeatAsTypeList<uint16_t, 6>,
    RepeatAsTypeList<uint32_t, 6>,
    RepeatAsTypeList<uint64_t, 6>,
    RepeatAsTypeList<float, 12>,
    RepeatAsTypeList<double, 12>,
    RepeatAsTypeList<std::string, 6>
>>;

// ============================================================================
// Profile → LayoutStatic dispatch
// ============================================================================

/// Dispatch a compile-time LayoutStatic type based on profile name.
/// Returns true if the profile has a known static layout; false otherwise.
template<typename Fn>
bool dispatchStaticLayoutForProfile(const bench::DatasetProfile& profile, Fn&& fn) {
    if (profile.name == "mixed_generic")            { fn.template operator()<MixedGenericLayoutStatic>(); return true; }
    if (profile.name == "sparse_events")            { fn.template operator()<SparseEventsLayoutStatic>(); return true; }
    if (profile.name == "sensor_noisy")             { fn.template operator()<SensorNoisyLayoutStatic>(); return true; }
    if (profile.name == "string_heavy")             { fn.template operator()<StringHeavyLayoutStatic>(); return true; }
    if (profile.name == "simulation_smooth")        { fn.template operator()<SimulationSmoothLayoutStatic>(); return true; }
    if (profile.name == "weather_timeseries")       { fn.template operator()<WeatherTimeseriesLayoutStatic>(); return true; }
    if (profile.name == "high_cardinality_string")  { fn.template operator()<HighCardinalityStringLayoutStatic>(); return true; }
    if (profile.name == "event_log")                { fn.template operator()<EventLogLayoutStatic>(); return true; }
    if (profile.name == "iot_fleet")                { fn.template operator()<IotFleetLayoutStatic>(); return true; }
    if (profile.name == "financial_orders")         { fn.template operator()<FinancialOrdersLayoutStatic>(); return true; }
    if (profile.name == "realistic_measurement")    { fn.template operator()<RealisticMeasurementLayoutStatic>(); return true; }
    if (profile.name == "measurement_campaign")     { fn.template operator()<MeasurementCampaignLayoutStatic>(); return true; }
    // bool_heavy (132), arithmetic_wide (200), rtl_waveform (290) — dynamic only
    return false;
}

} // namespace bench_static
