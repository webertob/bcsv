#pragma once

/**
 * @file bcsv.hpp
 * @brief Binary CSV (BCSV) Library - Shared implementations and utilities
 * 
 * This file contains implementations that require multiple headers to be included first.
 */

#include "file_header.h"
#include "layout.h"

namespace bcsv {

    // Type traits to check if StreamType supports file operations
    template<typename T>
    struct is_fstream {
        static constexpr bool value = std::is_same_v<T, std::fstream> || std::is_base_of_v<std::fstream, T>;
    };
    template<typename T>
    struct is_ifstream {
        static constexpr bool value = std::is_same_v<T, std::ifstream> || std::is_base_of_v<std::ifstream, T> || std::is_same_v<T, std::fstream> || std::is_base_of_v<std::fstream, T>;
    };

    template<typename T>
    struct is_ofstream {
        static constexpr bool value = std::is_same_v<T, std::ofstream> || std::is_base_of_v<std::ofstream, T> || std::is_same_v<T, std::fstream> || std::is_base_of_v<std::fstream, T>;
    };

    template<typename T>
    struct has_open_method {
        template<typename U>
        static auto test(int) -> decltype(std::declval<U>().open(std::string{}), std::true_type{});
        template<typename>
        static std::false_type test(...);
        using type = decltype(test<T>(0));
        static constexpr bool value = type::value;
    };

    template<typename T>
    struct has_close_method {
        template<typename U>
        static auto test(int) -> decltype(std::declval<U>().close(), std::true_type{});
        template<typename>
        static std::false_type test(...);
        using type = decltype(test<T>(0));
        static constexpr bool value = type::value;
    };

    template<typename T>
    struct has_is_open_method {
        template<typename U>
        static auto test(int) -> decltype(std::declval<U>().is_open(), std::true_type{});
        template<typename>
        static std::false_type test(...);
        using type = decltype(test<T>(0));
        static constexpr bool value = type::value;
    };

} // namespace bcsv
