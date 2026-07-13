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
 * @file spec_parse.h
 * @brief Shared SPEC grammar for per-column assignments (bcsvCast, csv2bcsv).
 *
 * Two forms, detected by the presence of '=':
 *   map form   'KEY=VALUE,KEY=VALUE,...'  — assign VALUE to the columns KEY selects
 *   list form  'VALUE,VALUE,...'          — one VALUE per column, must cover all
 *
 * A KEY is either an index expression in the bcsv_cli::parseIndexRanges grammar
 * (single index, i:j inclusive range, open ends, negatives from the end) or a
 * column NAME matched exactly against `names`. A key consisting only of range
 * syntax characters [0-9:+-] is always treated as an index expression, so
 * numeric-looking column names must be addressed by index.
 *
 * All parsers throw std::invalid_argument on grammar errors (callers map this
 * to their argument-error exit code).
 */

#include <cctype>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <bcsv/bcsv.h>
#include "cli_common.h"

namespace bcsv_cli {

namespace spec_detail {

    inline void trim(std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        s = s.substr(a, b - a);
    }

    // Strip optional surrounding braces and split on commas.
    inline std::vector<std::string> splitSpec(const std::string& spec_in) {
        std::string spec = spec_in;
        trim(spec);
        if (spec.size() >= 2 && spec.front() == '{' && spec.back() == '}')
            spec = spec.substr(1, spec.size() - 2);
        trim(spec);
        if (spec.empty())
            throw std::invalid_argument("Empty SPEC");

        std::vector<std::string> parts;
        std::string cur;
        for (char c : spec) {
            if (c == ',') { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        parts.push_back(cur);
        return parts;
    }

    inline bool looksLikeIndexExpr(const std::string& key) {
        for (char c : key) {
            if (!(std::isdigit((unsigned char)c) || c == ':' || c == '-' || c == '+'))
                return false;
        }
        return !key.empty();
    }

} // namespace spec_detail

/// Resolve a SPEC key to column indices. Index-range grammar first (a key made
/// only of [0-9:+-] is always an index expression); otherwise an exact, unique
/// match against `names`. Throws std::invalid_argument.
inline std::vector<size_t> resolveColumnKey(const std::string&              key,
                                            size_t                          n_columns,
                                            const std::vector<std::string>& names) {
    if (spec_detail::looksLikeIndexExpr(key)) {
        return parseIndexRanges(key, n_columns).toIndices(n_columns);
    }
    size_t found = n_columns;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == key) {
            if (found != n_columns)
                throw std::invalid_argument("Column name '" + key +
                                            "' is ambiguous (appears more than once); use the index");
            found = i;
        }
    }
    if (found == n_columns) {
        if (names.empty())
            throw std::invalid_argument("Key '" + key +
                                        "' is not an index expression, and no column names are "
                                        "available to match it against");
        throw std::invalid_argument("Unknown column name '" + key + "'");
    }
    return {found};
}

/// Column selection that accepts both index ranges and column names per
/// comma-separated token, e.g. 'time,3:5,-1' (shared by --cols style flags).
inline IndexRangeSet parseColumnSelection(const std::string&              spec,
                                          size_t                          n_columns,
                                          const std::vector<std::string>& names) {
    IndexRangeSet   out;
    std::vector<bool> selected(n_columns, false);
    for (auto& raw : spec_detail::splitSpec(spec)) {
        std::string tok = raw;
        spec_detail::trim(tok);
        if (tok.empty())
            throw std::invalid_argument("Empty entry in column selection (stray comma?)");
        for (size_t idx : resolveColumnKey(tok, n_columns, names))
            selected[idx] = true;
    }
    // Rebuild as merged ranges via the existing parser for canonical form.
    std::string canonical;
    for (size_t i = 0; i < n_columns; ++i) {
        if (!selected[i]) continue;
        size_t j = i;
        while (j + 1 < n_columns && selected[j + 1]) ++j;
        if (!canonical.empty()) canonical += ',';
        canonical += (i == j) ? std::to_string(i)
                              : std::to_string(i) + ":" + std::to_string(j);
        i = j;
    }
    return parseIndexRanges(canonical, n_columns);
}

/// Parse a type SPEC. Returns one entry per column: std::nullopt where the SPEC
/// leaves the column unspecified (map form) or says 'auto' (list form, when
/// allow_auto). 'void' is accepted only when allow_void (list form placeholder
/// for VOID columns; validated against the actual layout by the caller).
inline std::vector<std::optional<bcsv::ColumnType>>
parseTypeSpec(const std::string&              spec_in,
              size_t                          n_columns,
              const std::vector<std::string>& names      = {},
              bool                            allow_void = false,
              bool                            allow_auto = false) {
    using spec_detail::trim;

    std::vector<std::optional<bcsv::ColumnType>> req(n_columns);
    auto parts = spec_detail::splitSpec(spec_in);

    bool is_map = false;
    for (auto& p : parts)
        if (p.find('=') != std::string::npos) { is_map = true; break; }

    if (is_map) {
        std::vector<bool> seen(n_columns, false);
        for (auto& raw : parts) {
            std::string p = raw;
            trim(p);
            if (p.empty())
                throw std::invalid_argument("Empty entry in SPEC (stray comma?)");
            auto eq = p.find('=');
            if (eq == std::string::npos)
                throw std::invalid_argument("Mixed SPEC forms: entry '" + p +
                                            "' has no '=' but others do");
            std::string key = p.substr(0, eq);
            std::string typ = p.substr(eq + 1);
            trim(key);
            trim(typ);
            if (key.empty())
                throw std::invalid_argument("Empty column key in SPEC entry '" + p + "'");
            std::optional<bcsv::ColumnType> t;
            if (!(allow_auto && typ == "auto")) {
                try { t = parseColumnType(typ); }
                catch (const std::exception& e) { throw std::invalid_argument(std::string("SPEC: ") + e.what()); }
            }
            std::vector<size_t> indices;
            try { indices = resolveColumnKey(key, n_columns, names); }
            catch (const std::exception& e) { throw std::invalid_argument("SPEC key '" + key + "': " + e.what()); }
            for (size_t idx : indices) {
                if (seen[idx])
                    throw std::invalid_argument("Column " + std::to_string(idx) +
                                                " assigned more than once in SPEC");
                seen[idx] = true;
                req[idx]  = t;
            }
        }
    } else {
        if (parts.size() != n_columns)
            throw std::invalid_argument("Positional SPEC lists " + std::to_string(parts.size()) +
                                        " types but there are " + std::to_string(n_columns) +
                                        " columns — use the map form (e.g. '0=int32') for a subset");
        for (size_t i = 0; i < n_columns; ++i) {
            std::string p = parts[i];
            trim(p);
            if (p.empty())
                throw std::invalid_argument("Empty type at position " + std::to_string(i) + " in SPEC");
            if (allow_auto && p == "auto")
                continue;  // stays nullopt
            try { req[i] = parseColumnType(p, allow_void); }
            catch (const std::exception& e) {
                throw std::invalid_argument("SPEC position " + std::to_string(i) + ": " + e.what());
            }
        }
    }
    return req;
}

/// Parse a name SPEC (same map/list grammar; values are column names).
/// Map keys resolve against `names` (the current/original column names).
inline std::vector<std::optional<std::string>>
parseNameSpec(const std::string&              spec_in,
              size_t                          n_columns,
              const std::vector<std::string>& names = {}) {
    using spec_detail::trim;

    std::vector<std::optional<std::string>> out(n_columns);
    auto parts = spec_detail::splitSpec(spec_in);

    bool is_map = false;
    for (auto& p : parts)
        if (p.find('=') != std::string::npos) { is_map = true; break; }

    if (is_map) {
        std::vector<bool> seen(n_columns, false);
        for (auto& raw : parts) {
            std::string p = raw;
            trim(p);
            if (p.empty())
                throw std::invalid_argument("Empty entry in SPEC (stray comma?)");
            auto eq = p.find('=');
            if (eq == std::string::npos)
                throw std::invalid_argument("Mixed SPEC forms: entry '" + p +
                                            "' has no '=' but others do");
            std::string key  = p.substr(0, eq);
            std::string name = p.substr(eq + 1);
            trim(key);
            trim(name);
            if (key.empty())
                throw std::invalid_argument("Empty column key in SPEC entry '" + p + "'");
            if (name.empty())
                throw std::invalid_argument("Empty column name in SPEC entry '" + p + "'");
            std::vector<size_t> indices;
            try { indices = resolveColumnKey(key, n_columns, names); }
            catch (const std::exception& e) { throw std::invalid_argument("SPEC key '" + key + "': " + e.what()); }
            if (indices.size() != 1)
                throw std::invalid_argument("SPEC key '" + key +
                                            "' selects multiple columns — a name can only be "
                                            "assigned to a single column");
            if (seen[indices[0]])
                throw std::invalid_argument("Column " + std::to_string(indices[0]) +
                                            " assigned more than once in SPEC");
            seen[indices[0]] = true;
            out[indices[0]]  = name;
        }
    } else {
        if (parts.size() != n_columns)
            throw std::invalid_argument("Positional SPEC lists " + std::to_string(parts.size()) +
                                        " names but there are " + std::to_string(n_columns) +
                                        " columns");
        for (size_t i = 0; i < n_columns; ++i) {
            std::string p = parts[i];
            trim(p);
            if (p.empty())
                throw std::invalid_argument("Empty name at position " + std::to_string(i) + " in SPEC");
            out[i] = p;
        }
    }
    return out;
}

} // namespace bcsv_cli
