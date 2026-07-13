/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

/**
 * @file csv2bcsv.cpp
 * @brief CLI tool to convert CSV files to BCSV format
 *
 * Reads a CSV file and converts it to the binary BCSV format. Column types are
 * inferred from a sample of the data (default 1000 rows, --sample) using the
 * shared round-trip-exact probe (type_probe.h) and validated against EVERY row
 * during conversion: a later cell that does not fit the inferred type triggers
 * a full-file re-scan and one retry with the widened types, so no information
 * is ever silently truncated. Types and names can also be forced per column
 * (--types/--names, bcsvCast SPEC grammar), the header can be skipped or
 * absent, and row/column subsets can be selected (--rows/--cols).
 */

#include <cmath>
#include <iostream>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <bcsv/bcsv.h>
#include "cli_app.h"
#include "spec_parse.h"
#include "type_probe.h"

namespace fs = std::filesystem;

// Shared probe/cast machinery from type_probe.h.
using bcsv_cli::CellClass;
using bcsv_cli::CellValue;
using bcsv_cli::CsvColumnProbe;

// ── Arg error type — exit code 2 (same convention as bcsvCast) ──────

struct ArgError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

struct Config {
    std::string input_file;
    std::string output_file;
    char delimiter = '\0';  // '\0' means auto-detect
    char decimal_separator = '.';  // Default to point, can be changed to comma
    bool has_header = true;   // a header row exists (false with --no-header)
    bool skip_header = false; // consume and discard the header row
    bool verbose = false;
    bool force_delimiter = false;  // True if user explicitly set delimiter
    bool collapse_whitespace = false;  // Treat runs of spaces/tabs as one delimiter
    bool overwrite = false;
    bool benchmark = false;  // Print timing stats to stderr
    bool json_output = false;  // Emit JSON timing blob to stderr

    std::string names_spec;   // --names SPEC
    std::string types_spec;   // --types SPEC
    std::string rows_spec;    // --rows  (data-row indices, streaming: no negatives)
    std::string cols_spec;    // --cols  (CSV column indices or names)
    size_t sample_rows = 1000; // --sample N; 0 = scan the whole file before writing
    double tolerance   = 0.0;  // --tolerance (probe + FLOAT round-trip check)
    bool   strict      = false; // --strict: abort instead of clamping forced misfits
    bool   skip_partial_rows = false; // --skip-partial-rows: skip field-count mismatches
    bool   pad_partial_rows  = false; // --pad-partial-rows: pad short rows with empty cells

    // Codec selection (standardised with bcsvGenerator / bcsvSampler)
    std::string row_codec  = bcsv_cli::DEFAULT_ROW_CODEC;   // delta | zoh | flat
    std::string file_codec = bcsv_cli::DEFAULT_FILE_CODEC;  // packet_lz4_batch | ...
    size_t compression_level = 1;
    size_t block_size_kb     = bcsv::DEFAULT_PACKET_SIZE_KB;
};

// Automatic delimiter detection
static char detectDelimiter(const std::string& sample_line) {
    const std::vector<char> delimiters = {',', ';', '\t', '|'};
    std::map<char, int> delimiter_counts;

    bool in_quotes = false;
    char quote_char = '"';

    for (char c : sample_line) {
        if (c == quote_char) {
            in_quotes = !in_quotes;
        } else if (!in_quotes) {
            for (char delim : delimiters) {
                if (c == delim) {
                    delimiter_counts[delim]++;
                }
            }
        }
    }

    // Return delimiter with highest count
    char best_delimiter = ',';
    int max_count = 0;
    for (const auto& pair : delimiter_counts) {
        if (pair.second > max_count) {
            max_count = pair.second;
            best_delimiter = pair.first;
        }
    }

    return best_delimiter;
}

// ── Column plan ──────────────────────────────────────────────────────

struct ColumnPlan {
    std::string      name;
    bcsv::ColumnType type     = bcsv::ColumnType::STRING;
    bool             forced   = false;  // --types pinned it; never widened
    bool             selected = true;   // inside the --cols selection
    bool             overflow_warn = false;  // STRING because numeric data exceeds 64-bit/double
    uint64_t         clamped      = 0;  // forced-type cells saturated (write pass)
    uint64_t         unparseable  = 0;  // forced-type cells with unparseable content
};

// ── Partial-row policy ──────────────────────────────────────────────
//
// A row whose field count does not match the layout is most likely a corrupt
// row (CSVs are rarely populated partially), so the DEFAULT is to abort the
// conversion with a clear error. --skip-partial-rows skips such rows and
// --pad-partial-rows pads short rows with empty cells; both report how many
// rows were affected. Extra cells that are all empty (trailing delimiters)
// are always tolerated.

enum class RecordShape : uint8_t { Ok, Short, Long };

static RecordShape recordShape(const std::vector<std::string_view>& cells, size_t n_cols) {
    if (cells.size() == n_cols)
        return RecordShape::Ok;
    if (cells.size() < n_cols)
        return RecordShape::Short;
    for (size_t i = n_cols; i < cells.size(); ++i)
        if (!cells[i].empty())
            return RecordShape::Long;
    return RecordShape::Ok;   // only empty trailing cells (trailing delimiters)
}

enum class RowAction : uint8_t { Process, Skip };

// Apply the partial-row policy to one mismatched record. Returns Process
// (padded short row) or Skip; throws in the default strict mode.
static RowAction handlePartialRow(const Config& config, RecordShape shape,
                                  size_t row_idx, size_t file_line,
                                  size_t got, size_t expected,
                                  size_t& padded, size_t& skipped) {
    if (shape == RecordShape::Short && config.pad_partial_rows) {
        ++padded;
        return RowAction::Process;   // cellAt() supplies the empty cells
    }
    if (config.skip_partial_rows) {
        ++skipped;
        return RowAction::Skip;
    }
    throw std::runtime_error(
        "Data row " + std::to_string(row_idx) + " (file line " + std::to_string(file_line) +
        ") has " + std::to_string(got) + " fields, expected " + std::to_string(expected) +
        " — partial/malformed row, the CSV may be corrupt. Use --skip-partial-rows to skip "
        "such rows" +
        (shape == RecordShape::Short ? " or --pad-partial-rows to pad short rows" : "") + ".");
}

static inline std::string_view cellAt(const std::vector<std::string_view>& cells, size_t i) {
    return i < cells.size() ? cells[i] : std::string_view{};
}

// Trim the ' ' padding CsvReader::parseCells also strips for non-string cells.
static inline std::string_view trimSpaces(std::string_view v) {
    while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
    while (!v.empty() && v.back() == ' ')  v.remove_suffix(1);
    return v;
}

// Unquote a cell for numeric interpretation (cold path — quoted numerics).
static inline std::string_view numericView(std::string_view trimmed, std::string& unquote_buf) {
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        unquote_buf = bcsv::CsvReader<bcsv::Layout>::unquote(trimmed);
        std::string_view v = unquote_buf;
        return trimSpaces(v);
    }
    return trimmed;
}

// ── Shared reader setup ─────────────────────────────────────────────

// Open `reader` on the input in raw mode. Both passes read with an empty
// layout so tokenization (incl. splitLine's layout-based trailing-empty
// tolerance, which then never fires) is bit-identical between them; record
// shape is handled by recordShape()/handlePartialRow() instead.
static void openRaw(const Config& config, bcsv::CsvReader<bcsv::Layout>& reader) {
    if (!reader.open(config.input_file, /*hasHeader=*/false)) {
        throw std::runtime_error("Cannot open CSV file: " + reader.getErrorMsg());
    }
}

// Single definition of "the first record is a header to consume" — the probe
// and write passes must always agree on how many leading records to eat.
static inline bool consumesHeaderRecord(const Config& config) {
    return config.has_header || config.skip_header;
}

// ── Pass 1: probe (type inference + header/name/spec resolution) ────

struct ProbeResult {
    std::vector<ColumnPlan> plan;        // one per CSV column
    size_t                  n_csv_cols = 0;
};

static ProbeResult probePass(const Config& config, size_t sample_rows,
                             const std::vector<ColumnPlan>* prior /* rescue: keep names/forced */) {
    const bool consume_header = consumesHeaderRecord(config);
    bcsv::Layout empty_layout;
    bcsv::CsvReader<bcsv::Layout> reader(empty_layout, config.delimiter, config.decimal_separator,
                                         config.collapse_whitespace);
    openRaw(config, reader);

    ProbeResult result;

    // Header record: read it to learn the column count (and names unless
    // --skip-header discards them).
    std::vector<std::string> base_names;
    size_t n = 0;
    if (consume_header) {
        if (!reader.readNextRaw())
            throw std::runtime_error("Input file is empty");
        n = reader.rawCells().size();
        if (config.has_header) {
            for (const auto& c : reader.rawCells())
                base_names.push_back(bcsv::CsvReader<bcsv::Layout>::unquote(c));
            // Drop empty trailing names produced by trailing delimiters.
            while (!base_names.empty() && base_names.back().empty())
                base_names.pop_back();
            n = base_names.size();
        }
    }

    // Probe state; lazily sized on the first data record for headerless /
    // skip-header inputs.
    std::vector<CsvColumnProbe> probes;
    std::string                 scratch, unquote_buf;

    bcsv_cli::IndexRangeSet row_set = config.rows_spec.empty()
        ? bcsv_cli::IndexRangeSet{}
        : bcsv_cli::parseIndexRangesUnbounded(config.rows_spec);
    bcsv_cli::IndexRangeCursor row_cursor(row_set);

    // Column selection is resolved once n is known (below).
    std::optional<bcsv_cli::IndexRangeSet> col_set;
    std::vector<size_t> probe_cols;   // selected, non-forced columns to probe
    std::vector<std::optional<bcsv::ColumnType>> forced;

    auto finalizeColumns = [&](size_t n_cols) {
        n = n_cols;
        if (base_names.empty())
            for (size_t i = 0; i < n; ++i)
                base_names.push_back("column_" + std::to_string(i + 1));
        if (base_names.size() != n)
            throw std::runtime_error("Header lists " + std::to_string(base_names.size()) +
                                     " columns but the first data row has " + std::to_string(n));

        // --names: override/replace names (map keys resolve against base names).
        std::vector<std::string> final_names = base_names;
        if (!config.names_spec.empty()) {
            std::vector<std::optional<std::string>> spec_names;
            try {
                spec_names = bcsv_cli::parseNameSpec(config.names_spec, n, base_names);
            } catch (const std::exception& e) {
                throw ArgError(std::string("--names: ") + e.what());
            }
            for (size_t i = 0; i < n; ++i)
                if (spec_names[i])
                    final_names[i] = *spec_names[i];
        }
        for (size_t i = 0; i < n; ++i) {
            if (final_names[i].empty())
                throw ArgError("Column " + std::to_string(i) + " has an empty name "
                               "(provide one with --names)");
            for (size_t j = i + 1; j < n; ++j)
                if (final_names[i] == final_names[j])
                    throw ArgError("Duplicate column name '" + final_names[i] + "' (columns " +
                                   std::to_string(i) + " and " + std::to_string(j) +
                                   ") — rename with --names");
        }

        // --cols: selection over CSV columns (indices or final names).
        std::vector<char> selected(n, 1);
        if (!config.cols_spec.empty()) {
            try {
                col_set = bcsv_cli::parseColumnSelection(config.cols_spec, n, final_names);
            } catch (const std::exception& e) {
                throw ArgError(std::string("--cols: ") + e.what());
            }
            for (size_t i = 0; i < n; ++i)
                selected[i] = col_set->contains(i) ? 1 : 0;
        }

        // --types: forced types (keys resolve against final names).
        forced.assign(n, std::nullopt);
        if (!config.types_spec.empty()) {
            try {
                forced = bcsv_cli::parseTypeSpec(config.types_spec, n, final_names,
                                                 /*allow_void=*/false, /*allow_auto=*/true);
            } catch (const std::exception& e) {
                throw ArgError(std::string("--types: ") + e.what());
            }
            for (size_t i = 0; i < n; ++i)
                if (forced[i] && !selected[i])
                    throw ArgError("--types assigns column " + std::to_string(i) + " ('" +
                                   final_names[i] + "') which is outside the --cols selection");
        }

        // Rescue pass: the file is being read a second time — if its column
        // count changed underneath us, fail cleanly instead of indexing the
        // prior plan out of bounds.
        if (prior && prior->size() != n)
            throw std::runtime_error("Input file changed during conversion (had " +
                                     std::to_string(prior->size()) + " columns, now " +
                                     std::to_string(n) + ")");

        result.plan.resize(n);
        probes.resize(n);
        for (size_t i = 0; i < n; ++i) {
            ColumnPlan& c = result.plan[i];
            c.name     = final_names[i];
            c.selected = selected[i] != 0;
            if (prior) {
                // Rescue pass: keep the established plan; only re-derive
                // non-forced selected columns.
                c.forced = (*prior)[i].forced;
                c.type   = (*prior)[i].type;
            } else if (forced[i]) {
                c.forced = true;
                c.type   = *forced[i];
            }
            if (c.selected && !c.forced) {
                probes[i].init(config.tolerance);
                probe_cols.push_back(i);
            }
        }
        result.n_csv_cols = n;
    };

    if (consume_header && config.has_header)
        finalizeColumns(n);

    // ── Sample loop ──
    size_t data_row = 0;     // index over CSV data records (selection domain)
    size_t sampled  = 0;     // selected rows probed
    size_t padded = 0, skipped = 0;   // partial-row policy bookkeeping (reported by the write pass)
    while (reader.readNextRaw()) {
        const auto& cells = reader.rawCells();
        if (result.plan.empty()) {
            // Headerless/skip-header: the column count comes from the first
            // data row, with trailing empty cells trimmed (trailing-delimiter
            // files), mirroring the header-name trimming above.
            size_t n_first = cells.size();
            while (n_first > 0 && cells[n_first - 1].empty())
                --n_first;
            finalizeColumns(n_first);
        }

        const size_t row_idx = data_row++;
        if (!row_cursor.contains(row_idx))
            continue;
        const RecordShape shape = recordShape(cells, result.n_csv_cols);
        if (shape != RecordShape::Ok &&
            handlePartialRow(config, shape, row_idx, reader.fileLine(),
                             cells.size(), result.n_csv_cols, padded, skipped) == RowAction::Skip)
            continue;

        for (size_t k = 0; k < probe_cols.size(); ++k) {
            const size_t i = probe_cols[k];
            if (probes[i].settled())
                continue;   // type can no longer change — skip the cell
            std::string_view cell = numericView(trimSpaces(cellAt(cells, i)), unquote_buf);
            probes[i].visit(cell, config.decimal_separator, scratch);
        }

        if (++sampled == sample_rows && sample_rows != 0)
            break;
        if (row_cursor.exhausted(data_row))
            break;
    }
    reader.close();

    if (result.plan.empty())
        throw std::runtime_error(consumesHeaderRecord(config)
                                 ? "No valid data rows found"
                                 : "Input file is empty");
    if (data_row == 0)
        throw std::runtime_error("No valid data rows found");

    for (size_t i = 0; i < result.n_csv_cols; ++i) {
        ColumnPlan& c = result.plan[i];
        if (c.selected && !c.forced) {
            c.type          = probes[i].derive();
            c.overflow_warn = probes[i].overflowWarning();
        }
    }
    return result;
}

// ── Pass 2: checked conversion ──────────────────────────────────────

struct WriteResult {
    bool        ok = false;
    size_t      rows_written = 0;
    size_t      padded_rows  = 0;  // short rows padded (--pad-partial-rows)
    size_t      skipped_rows = 0;  // mismatched rows skipped (--skip-partial-rows)
    // First misfit (inferred column whose data no longer fits):
    size_t      misfit_row = 0;
    size_t      misfit_col = 0;
    std::string misfit_cell;
    // First forced-type misfit under --strict:
    bool        strict_abort = false;
};

enum class CellFit : uint8_t { Ok, Misfit };

// Cold path: an integer/bool-typed cell that from_chars could not fully
// consume or that overflowed. Retries an exact fit first (covers '+'-prefixed
// and quoted numerics), then "3.0"-style whole floats within tolerance; only
// genuinely unfittable values become misfits (or saturating clamps for forced
// columns). Kept out of the hot switch.
template<typename RowType>
static CellFit intSlowPath(bcsv::ColumnType t, std::string_view cell, char decimal_sep,
                           double tol, RowType& row, size_t out_col,
                           ColumnPlan& plan, std::string& scratch) {
    CellValue v;
    const CellClass cls = bcsv_cli::classifyCell(cell, decimal_sep, scratch, v);

    auto storeValue = [&](const bcsv::ValueType& val) {
        std::visit([&](const auto& x) { row.set(out_col, x); }, val);
    };

    uint64_t oor = 0, unp = 0;
    uint64_t no_clamp = 0;
    switch (cls) {
        case CellClass::IntUnsigned:
            if (!bcsv_cli::cellLoses<uint64_t>(t, v.u, tol, oor, unp)) {
                storeValue(bcsv_cli::coerce<uint64_t>(t, v.u, tol, no_clamp));
                return CellFit::Ok;
            }
            if (!plan.forced)
                return CellFit::Misfit;
            storeValue(bcsv_cli::coerce<uint64_t>(t, v.u, tol, plan.clamped));
            return CellFit::Ok;
        case CellClass::IntSigned:
            if (!bcsv_cli::cellLoses<int64_t>(t, v.i, tol, oor, unp)) {
                storeValue(bcsv_cli::coerce<int64_t>(t, v.i, tol, no_clamp));
                return CellFit::Ok;
            }
            if (!plan.forced)
                return CellFit::Misfit;
            storeValue(bcsv_cli::coerce<int64_t>(t, v.i, tol, plan.clamped));
            return CellFit::Ok;
        case CellClass::FloatNum: {
            // Whole-within-tolerance floats convert losslessly (matches the
            // probe's integer ladder).
            const double r = std::round(v.d);
            if (std::fabs(v.d - r) <= tol && bcsv_cli::roundedFitsInt(t, r)) {
                storeValue(bcsv_cli::doubleToIntForced(t, v.d, tol, no_clamp));
                return CellFit::Ok;
            }
            if (!plan.forced)
                return CellFit::Misfit;
            storeValue(bcsv_cli::doubleToIntForced(t, v.d, tol, plan.clamped));
            return CellFit::Ok;
        }
        case CellClass::NonFinite:
            if (!plan.forced)
                return CellFit::Misfit;
            storeValue(bcsv_cli::doubleToIntForced(t, v.d, tol, plan.clamped));
            return CellFit::Ok;
        case CellClass::HugeInt:
        case CellClass::HugeFloat: {
            if (!plan.forced)
                return CellFit::Misfit;
            // Sign decides the saturation end.
            const double approx = (!cell.empty() && cell.front() == '-')
                                  ? -bcsv_cli::TWO_POW_64 : bcsv_cli::TWO_POW_64;
            storeValue(bcsv_cli::doubleToIntForced(t, approx, tol, plan.clamped));
            return CellFit::Ok;
        }
        default:  // NonNumeric (Empty is handled by the caller)
            if (!plan.forced)
                return CellFit::Misfit;
            ++plan.unparseable;
            if (t == bcsv::ColumnType::BOOL)
                row.set(out_col, false);
            else
                storeValue(bcsv_cli::zeroOf(t));
            return CellFit::Ok;
    }
}

// Cold path for FLOAT/DOUBLE cells that failed the fast parse or the FLOAT
// round-trip test.
template<typename RowType>
static CellFit floatSlowPath(bcsv::ColumnType t, std::string_view cell, char decimal_sep,
                             double tol, RowType& row, size_t out_col,
                             ColumnPlan& plan, std::string& scratch) {
    CellValue v;
    const CellClass cls = bcsv_cli::classifyCell(cell, decimal_sep, scratch, v);

    double d;
    switch (cls) {
        case CellClass::IntUnsigned:
        case CellClass::IntSigned: {
            // An integer that is not exactly double-representable would be
            // silently rounded — that is a misfit (the re-probe turns the
            // column into STRING with a warning).
            const uint64_t mag = (cls == CellClass::IntUnsigned) ? v.u : bcsv_cli::absMag(v.i);
            if (!bcsv_cli::magFitsMantissa(mag, 53)) {
                if (!plan.forced)
                    return CellFit::Misfit;
                ++plan.clamped;
            }
            d = (cls == CellClass::IntUnsigned) ? static_cast<double>(v.u)
                                                : static_cast<double>(v.i);
            break;
        }
        case CellClass::FloatNum:
        case CellClass::NonFinite:
            d = v.d;
            break;
        case CellClass::HugeInt:
        case CellClass::HugeFloat:
            if (!plan.forced)
                return CellFit::Misfit;
            ++plan.clamped;
            d = (!cell.empty() && cell.front() == '-')
                ? -std::numeric_limits<double>::infinity()
                :  std::numeric_limits<double>::infinity();
            break;
        default:
            if (!plan.forced)
                return CellFit::Misfit;
            ++plan.unparseable;
            d = 0.0;
            break;
    }

    if (t == bcsv::ColumnType::FLOAT) {
        const float f = static_cast<float>(d);
        if (!std::isnan(d) && std::fabs(static_cast<double>(f) - d) > tol) {
            if (!plan.forced)
                return CellFit::Misfit;
            ++plan.clamped;
        }
        row.set(out_col, f);
    } else {
        row.set(out_col, d);
    }
    return CellFit::Ok;
}

// Checked conversion of one cell into the writer row. Hot path: one switch,
// one from_chars, no allocations; anything unusual defers to the cold helpers.
template<typename RowType>
static inline CellFit parseCellChecked(bcsv::ColumnType t, std::string_view raw_cell,
                                       char decimal_sep, double tol,
                                       RowType& row, size_t out_col,
                                       ColumnPlan& plan,
                                       std::string& scratch, std::string& unquote_buf,
                                       std::string& str_buf) {
    if (t == bcsv::ColumnType::STRING) {
        // Reuse str_buf's capacity for the common unquoted case — unquote()
        // returns a fresh allocation, which would defeat the scratch buffer.
        if (raw_cell.size() >= 2 && raw_cell.front() == '"' && raw_cell.back() == '"')
            str_buf = bcsv::CsvReader<bcsv::Layout>::unquote(raw_cell);
        else
            str_buf.assign(raw_cell.data(), raw_cell.size());
        row.set(out_col, str_buf);
        return CellFit::Ok;
    }

    std::string_view cell = trimSpaces(raw_cell);
    if (!cell.empty() && cell.front() == '"')
        cell = numericView(cell, unquote_buf);

    switch (t) {
        case bcsv::ColumnType::BOOL: {
            bool v = false;
            if (cell.empty() || bcsv_cli::parseBoolToken(cell, v)) {
                row.set(out_col, v);
                return CellFit::Ok;
            }
            return intSlowPath(t, cell, decimal_sep, tol, row, out_col, plan, scratch);
        }
        case bcsv::ColumnType::INT8:   case bcsv::ColumnType::INT16:
        case bcsv::ColumnType::INT32:  case bcsv::ColumnType::INT64:
        case bcsv::ColumnType::UINT8:  case bcsv::ColumnType::UINT16:
        case bcsv::ColumnType::UINT32: case bcsv::ColumnType::UINT64: {
            if (cell.empty()) {
                std::visit([&](const auto& x) { row.set(out_col, x); }, bcsv_cli::zeroOf(t));
                return CellFit::Ok;
            }
            // One fast exact parse per width; anything unusual defers to the
            // slow path. The template lambda keeps the eight cases in lockstep.
            auto tryExact = [&]<typename T>() -> bool {
                T v{};
                auto [p, ec] = std::from_chars(cell.data(), cell.data() + cell.size(), v);
                if (p == cell.data() + cell.size() && ec == std::errc{}) {
                    row.set(out_col, v);
                    return true;
                }
                return false;
            };
            bool ok = false;
            switch (t) {
                case bcsv::ColumnType::INT8:   ok = tryExact.template operator()<int8_t>();   break;
                case bcsv::ColumnType::INT16:  ok = tryExact.template operator()<int16_t>();  break;
                case bcsv::ColumnType::INT32:  ok = tryExact.template operator()<int32_t>();  break;
                case bcsv::ColumnType::INT64:  ok = tryExact.template operator()<int64_t>();  break;
                case bcsv::ColumnType::UINT8:  ok = tryExact.template operator()<uint8_t>();  break;
                case bcsv::ColumnType::UINT16: ok = tryExact.template operator()<uint16_t>(); break;
                case bcsv::ColumnType::UINT32: ok = tryExact.template operator()<uint32_t>(); break;
                default:                       ok = tryExact.template operator()<uint64_t>(); break;
            }
            if (ok)
                return CellFit::Ok;
            return intSlowPath(t, cell, decimal_sep, tol, row, out_col, plan, scratch);
        }
        case bcsv::ColumnType::FLOAT: {
            if (cell.empty()) {
                row.set(out_col, 0.0f);
                return CellFit::Ok;
            }
            double d{};
            const char* b = cell.data();
            const char* e = b + cell.size();
            if (decimal_sep == '.') {
                if (auto [p, ec] = bcsv::compat::from_chars(b, e, d); p == e && ec == std::errc{}) {
                    const float f = static_cast<float>(d);
                    if (std::isnan(d) || std::fabs(static_cast<double>(f) - d) <= tol) {
                        row.set(out_col, f);
                        return CellFit::Ok;
                    }
                }
            }
            return floatSlowPath(t, cell, decimal_sep, tol, row, out_col, plan, scratch);
        }
        case bcsv::ColumnType::DOUBLE: {
            if (cell.empty()) {
                row.set(out_col, 0.0);
                return CellFit::Ok;
            }
            double d{};
            const char* b = cell.data();
            const char* e = b + cell.size();
            if (decimal_sep == '.') {
                if (auto [p, ec] = bcsv::compat::from_chars(b, e, d); p == e && ec == std::errc{}) {
                    row.set(out_col, d);
                    return CellFit::Ok;
                }
            }
            return floatSlowPath(t, cell, decimal_sep, tol, row, out_col, plan, scratch);
        }
        default:
            return CellFit::Misfit;  // VOID — cannot appear in a csv2bcsv layout
    }
}

static WriteResult writePass(const Config& config, std::vector<ColumnPlan>& plan,
                             size_t n_csv_cols, const bcsv::Layout& out_layout,
                             const fs::path& tmp_path) {
    WriteResult result;

    // Selected columns → output column index, packed for the hot loop.
    std::vector<size_t>           csv_cols;
    std::vector<bcsv::ColumnType> types;
    std::vector<ColumnPlan*>      plans;
    for (size_t i = 0; i < n_csv_cols; ++i) {
        if (!plan[i].selected)
            continue;
        csv_cols.push_back(i);
        types.push_back(plan[i].type);
        plans.push_back(&plan[i]);
    }

    bcsv_cli::IndexRangeSet row_set = config.rows_spec.empty()
        ? bcsv_cli::IndexRangeSet{}
        : bcsv_cli::parseIndexRangesUnbounded(config.rows_spec);
    bcsv_cli::IndexRangeCursor row_cursor(row_set);

    auto codec_settings = bcsv_cli::resolveCodecFlags(
        config.file_codec, config.row_codec, config.compression_level);

    bool write_ok = true;
    auto write_rows = [&](auto& writer) {
        if (!writer.open(tmp_path.string(), /*overwrite=*/true,
                         codec_settings.comp_level, config.block_size_kb,
                         codec_settings.flags)) {
            throw std::runtime_error("Cannot open output file: " + tmp_path.string() +
                                     " (" + writer.getErrorMsg() + ")");
        }

        bcsv::Layout empty_layout;
        bcsv::CsvReader<bcsv::Layout> reader(empty_layout, config.delimiter,
                                             config.decimal_separator,
                                             config.collapse_whitespace);
        openRaw(config, reader);
        if (consumesHeaderRecord(config) && !reader.readNextRaw())
            throw std::runtime_error("Input file is empty");

        std::string scratch, unquote_buf, str_buf;
        size_t data_row = 0;

        while (reader.readNextRaw()) {
            const auto& cells = reader.rawCells();
            const size_t row_idx = data_row++;
            if (!row_cursor.contains(row_idx)) {
                if (row_cursor.exhausted(data_row))
                    break;
                continue;
            }
            const RecordShape shape = recordShape(cells, n_csv_cols);
            if (shape != RecordShape::Ok &&
                handlePartialRow(config, shape, row_idx, reader.fileLine(),
                                 cells.size(), n_csv_cols,
                                 result.padded_rows, result.skipped_rows) == RowAction::Skip)
                continue;

            auto& row = writer.row();
            for (size_t k = 0; k < csv_cols.size(); ++k) {
                const std::string_view cell = cellAt(cells, csv_cols[k]);
                const CellFit fit = parseCellChecked(types[k], cell, config.decimal_separator,
                                                     config.tolerance, row, k, *plans[k],
                                                     scratch, unquote_buf, str_buf);
                if (fit == CellFit::Misfit) {
                    result.misfit_row  = row_idx;
                    result.misfit_col  = csv_cols[k];
                    result.misfit_cell = std::string(cell);
                    write_ok = false;
                    break;
                }
                if (config.strict && (plans[k]->clamped || plans[k]->unparseable)) {
                    result.misfit_row  = row_idx;
                    result.misfit_col  = csv_cols[k];
                    result.misfit_cell = std::string(cell);
                    result.strict_abort = true;
                    write_ok = false;
                    break;
                }
            }
            if (!write_ok)
                break;

            writer.writeRow();
            ++result.rows_written;

            if (config.verbose && (result.rows_written & 0x3FFF) == 0)
                std::cerr << "Processed " << result.rows_written << " rows..." << std::endl;
            if (row_cursor.exhausted(data_row))
                break;
        }

        reader.close();
        writer.close();
    };

    bcsv_cli::withWriter(out_layout, config.row_codec, write_rows);
    result.ok = write_ok;
    return result;
}

// Build the output layout from the selected columns of the plan.
static bcsv::Layout buildLayout(const std::vector<ColumnPlan>& plan) {
    bcsv::Layout layout;
    for (const auto& c : plan)
        if (c.selected)
            layout.addColumn(bcsv::ColumnDefinition(c.name, c.type));
    return layout;
}

int main(int argc, char* argv[]) {
    Config config;
    bool   no_header = false;

    CLI::App app{"Convert CSV file to BCSV format.", "csv2bcsv"};
    argv = app.ensure_utf8(argv);
    bcsv_cli::setupVersionFlag(app, bcsv_cli::programName(argv[0]));

    auto* delim_opt = app.add_option("-d,--delimiter", config.delimiter,
                                     "Field delimiter (default: auto-detect)");
    app.add_flag("-w,--whitespace", config.collapse_whitespace,
                 "Treat runs of spaces/tabs as one delimiter (also splits header names)");
    auto* no_header_flag = app.add_flag("--no-header", no_header, "CSV file has no header row");
    app.add_flag("--skip-header", config.skip_header,
                 "Consume and discard the header row (names from --names or auto-generated)")
        ->excludes(no_header_flag);
    app.add_option("--names", config.names_spec,
                   "Column names: map '0=time,temp=temp_c' or list 'a,b,c' (all columns)")
        ->type_name("SPEC");
    app.add_option("--types", config.types_spec,
                   "Force column types: map '0=int32,price=float,7:8=double' or list "
                   "'int32,auto,...' (auto = infer)")
        ->type_name("SPEC");
    app.add_option("--rows", config.rows_spec,
                   "Convert only these data rows, e.g. '0:999,5000:' (0-based, after the header; "
                   "no negative indices)")
        ->type_name("RANGES");
    app.add_option("--cols", config.cols_spec,
                   "Convert only these columns, by index range or name, e.g. '0:3,temp,-1'")
        ->type_name("RANGES");
    app.add_option("--sample", config.sample_rows,
                   "Rows sampled for type inference; 0 = scan the whole file before writing")
        ->capture_default_str();
    app.add_option("--tolerance", config.tolerance,
                   "Absolute epsilon for float narrowing/round-trip tests")
        ->check(bcsv_cli::validateToleranceArg)
        ->capture_default_str();
    app.add_flag("--strict", config.strict,
                 "Abort (exit 1) when a cell does not fit a forced --types column "
                 "instead of clamping it");
    app.add_flag("--skip-partial-rows", config.skip_partial_rows,
                 "Skip rows whose field count does not match the columns "
                 "(default: abort — such rows usually indicate a corrupt CSV)");
    app.add_flag("--pad-partial-rows", config.pad_partial_rows,
                 "Pad short rows with empty cells instead of aborting "
                 "(rows with excess fields still abort unless --skip-partial-rows)");
    app.add_option("--decimal-separator", config.decimal_separator,
                   "Decimal separator: '.' or ','")
        ->check([](const std::string& s) -> std::string {
            if (s.size() == 1 && (s[0] == '.' || s[0] == ','))
                return {};
            return "Decimal separator must be '.' or ','";
        });
    app.add_flag("-f,--overwrite", config.overwrite, "Overwrite output file if it exists");
    app.add_flag("-v,--verbose", config.verbose, "Enable verbose output");
    app.add_flag("--benchmark", config.benchmark,
                 "Print timing stats (wall clock, rows/s, MB/s) to stderr");
    app.add_flag("--json", config.json_output,
                 "With --benchmark: emit JSON timing blob to stderr");
    bcsv_cli::addCodecOptions(app, config.row_codec, config.file_codec,
                              config.compression_level, config.block_size_kb);
    app.add_option("INPUT_FILE", config.input_file, "Input CSV file path")
        ->required();
    app.add_option("OUTPUT_FILE", config.output_file,
                   "Output BCSV file path (default: <input>.bcsv)");

    app.footer(
        "Type inference samples the first --sample rows (default 1000) and keeps\n"
        "validating every row during conversion; if a later cell exceeds the inferred\n"
        "type, the file is re-scanned and converted once more with the widened type\n"
        "(ints grow to int64/uint64, floats to double, beyond that the column becomes\n"
        "STRING with a warning). Forced --types columns clamp instead (see --strict).\n\n"
        "SPEC (quote it — shells expand unquoted { }):\n"
        "  map form   '0=int32,price=float,7:8=double'  (index, i:j range, or name = value)\n"
        "  list form  'int32,auto,double,...'           (one entry per column, all columns)\n\n"
        "Examples:\n"
        "  csv2bcsv data.csv\n"
        "  csv2bcsv -d ';' data.csv output.bcsv\n"
        "  csv2bcsv --skip-header --names 'time,value' data.csv\n"
        "  csv2bcsv --types 'id=uint64,3:5=float' --cols '0:5' data.csv\n"
        "  csv2bcsv --rows '0:9999' --sample 0 data.csv\n"
        "  csv2bcsv --row-codec zoh data.csv");

    if (auto rc = bcsv_cli::parseHandled(app, argc, argv, 2)) return *rc;

    try {
        try {
            config.has_header      = !(no_header || config.skip_header);
            config.force_delimiter = (delim_opt->count() > 0);

            // A positional containing '=' is almost always a brace-expanded SPEC
            // (fish/bash split {a=b,c=d} into words).
            for (const std::string* p : {&config.input_file, &config.output_file})
                if (p->find('=') != std::string::npos)
                    throw ArgError("Positional argument '" + *p +
                                   "' looks like a SPEC — quote the SPEC and pass it "
                                   "with --types/--names (shells expand unquoted { }).");

            // Default output file if not specified
            if (config.output_file.empty() && !config.input_file.empty()) {
                fs::path input_path(config.input_file);
                config.output_file = input_path.stem().string() + ".bcsv";
            }

            // Delimiter and decimal separator must differ (unless whitespace-collapse)
            if (!config.collapse_whitespace &&
                config.delimiter == config.decimal_separator && config.delimiter != '\0') {
                throw ArgError("Delimiter and decimal separator cannot be the same ('" +
                               std::string(1, config.delimiter) + "')");
            }

            // Validate row spec up front for a clean exit-2 (negatives etc.)
            if (!config.rows_spec.empty()) {
                try {
                    bcsv_cli::parseIndexRangesUnbounded(config.rows_spec);
                } catch (const std::exception& e) {
                    throw ArgError(std::string("--rows: ") + e.what());
                }
            }
        } catch (const ArgError& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 2;
        }

        // Check if input file exists
        if (!fs::exists(config.input_file)) {
            throw std::runtime_error("Input file does not exist: " + config.input_file);
        }
        if (fs::exists(config.output_file) && !config.overwrite) {
            throw std::runtime_error("Output file already exists: " + config.output_file +
                                     " (use -f/--overwrite)");
        }

        // Get input file size for compression statistics
        auto input_file_size = fs::file_size(config.input_file);

        // Start timing the conversion process
        auto start_time = std::chrono::steady_clock::now();

        // Auto-detect delimiter from the first physical line (even when the
        // header row is skipped).
        if (!config.collapse_whitespace && !config.force_delimiter) {
            std::ifstream input(config.input_file);
            if (!input.is_open())
                throw std::runtime_error("Cannot open input file: " + config.input_file);
            std::string line;
            if (!std::getline(input, line))
                throw std::runtime_error("Input file is empty");
            config.delimiter = detectDelimiter(line);
            if (config.verbose)
                std::cerr << "Auto-detected delimiter: '" << config.delimiter << "'" << std::endl;
        }

        if (config.verbose) {
            std::cerr << "Converting: " << config.input_file << " -> " << config.output_file << std::endl;
            if (config.collapse_whitespace)
                std::cerr << "Delimiter: whitespace-collapse" << std::endl;
            else
                std::cerr << "Delimiter: '" << config.delimiter << "'" << std::endl;
            std::cerr << "Header: " << (config.has_header ? "yes" : (config.skip_header ? "skipped" : "no")) << std::endl;
            std::cerr << "Decimal separator: '" << config.decimal_separator << "'" << std::endl;
            std::cerr << "Encoding: " << bcsv_cli::encodingDescription(
                config.row_codec, config.file_codec, config.compression_level) << std::endl;
        }

        // ── Pass 1: probe ──
        ProbeResult probe;
        try {
            probe = probePass(config, config.sample_rows, nullptr);
        } catch (const ArgError& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 2;
        }

        size_t passes = 2;
        size_t widened_columns = 0;

        if (config.verbose) {
            std::cerr << "Detected " << probe.n_csv_cols << " columns:" << std::endl;
            for (const auto& c : probe.plan)
                if (c.selected)
                    std::cerr << "  " << c.name << " -> " << bcsv_cli::columnTypeStr(c.type)
                              << (c.forced ? " (forced)" : "") << std::endl;
        }

        // ── Pass 2 (+ optional rescue): checked conversion ──
        WriteResult wres;
        for (int attempt = 0; ; ++attempt) {
            bcsv::Layout layout = buildLayout(probe.plan);
            fs::path     tmp    = bcsv_cli::makeTempSibling(fs::path(config.output_file), "csv2bcsv");
            try {
                wres = writePass(config, probe.plan, probe.n_csv_cols, layout, tmp);
                if (wres.ok) {
                    fs::rename(tmp, config.output_file);
                    break;
                }
                fs::remove(tmp);
            } catch (...) {
                std::error_code ec;
                fs::remove(tmp, ec);
                throw;
            }

            if (wres.strict_abort) {
                throw std::runtime_error(
                    "--strict: cell '" + wres.misfit_cell + "' at data row " +
                    std::to_string(wres.misfit_row) + ", column " + std::to_string(wres.misfit_col) +
                    " ('" + probe.plan[wres.misfit_col].name + "') does not fit forced type " +
                    bcsv_cli::columnTypeStr(probe.plan[wres.misfit_col].type));
            }
            if (attempt >= 1) {
                throw std::runtime_error(
                    "Cell '" + wres.misfit_cell + "' at data row " + std::to_string(wres.misfit_row) +
                    ", column " + std::to_string(wres.misfit_col) +
                    " still does not fit after re-scanning — did the input change during conversion?");
            }

            // Widen: full-file re-probe, then one retry.
            if (config.verbose)
                std::cerr << "Cell '" << wres.misfit_cell << "' at data row " << wres.misfit_row
                          << ", column " << wres.misfit_col << " ('"
                          << probe.plan[wres.misfit_col].name << "') does not fit "
                          << bcsv_cli::columnTypeStr(probe.plan[wres.misfit_col].type)
                          << " — re-scanning the whole file" << std::endl;

            std::vector<ColumnPlan> old_plan = probe.plan;
            ProbeResult rescan = probePass(config, 0, &old_plan);
            for (size_t i = 0; i < probe.n_csv_cols; ++i) {
                // Reset per-column counters for the retry write.
                rescan.plan[i].clamped     = 0;
                rescan.plan[i].unparseable = 0;
                if (rescan.plan[i].type != old_plan[i].type)
                    ++widened_columns;
            }
            probe.plan = std::move(rescan.plan);
            passes     = 3;

            if (config.verbose) {
                std::cerr << "Widened types:" << std::endl;
                for (size_t i = 0; i < probe.n_csv_cols; ++i)
                    if (probe.plan[i].selected && probe.plan[i].type != old_plan[i].type)
                        std::cerr << "  " << probe.plan[i].name << ": "
                                  << bcsv_cli::columnTypeStr(old_plan[i].type) << " -> "
                                  << bcsv_cli::columnTypeStr(probe.plan[i].type) << std::endl;
            }
        }
        const size_t row_count = wres.rows_written;

        // ── Warnings ──
        if (wres.skipped_rows > 0)
            std::cerr << "Warning: skipped " << wres.skipped_rows
                      << " partial row(s) with mismatched field counts "
                      << "(--skip-partial-rows)" << std::endl;
        if (wres.padded_rows > 0)
            std::cerr << "Warning: padded " << wres.padded_rows
                      << " short row(s) with empty cells (--pad-partial-rows)" << std::endl;
        for (size_t i = 0; i < probe.n_csv_cols; ++i) {
            const ColumnPlan& c = probe.plan[i];
            if (!c.selected)
                continue;
            if (c.overflow_warn)
                std::cerr << "Warning: column " << i << " ('" << c.name << "') contains numeric "
                          << "values that exceed the 64-bit integer / double range; kept as "
                          << "STRING to avoid data loss" << std::endl;
            if (c.clamped)
                std::cerr << "Warning: column " << i << " ('" << c.name << "'): " << c.clamped
                          << " value(s) clamped to fit forced type "
                          << bcsv_cli::columnTypeStr(c.type) << std::endl;
            if (c.unparseable)
                std::cerr << "Warning: column " << i << " ('" << c.name << "'): " << c.unparseable
                          << " unparseable value(s) stored as defaults in forced type "
                          << bcsv_cli::columnTypeStr(c.type) << std::endl;
        }

        // Calculate conversion timing and statistics
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        auto output_file_size = fs::file_size(config.output_file);

        // Ensure minimum duration for throughput calculation
        long long duration_ms = duration.count();
        if (duration_ms == 0) duration_ms = 1;  // Minimum 1ms for calculation
        double duration_seconds = duration_ms / 1000.0;

        // Calculate compression ratio and throughput
        double compression_ratio = (static_cast<double>(input_file_size - output_file_size) / input_file_size) * 100.0;
        double throughput_mb_s = (static_cast<double>(input_file_size) / (1024.0 * 1024.0)) / duration_seconds;
        double rows_per_sec = static_cast<double>(row_count) / duration_seconds;

        const size_t out_columns = buildLayout(probe.plan).columnCount();

        // Display comprehensive conversion statistics
        std::cerr << "\n=== Conversion Complete ==="<< std::endl;
        std::cerr << "Successfully converted " << row_count << " rows to " << config.output_file << std::endl;
        std::cerr << "Columns detected: " << out_columns << std::endl;
        std::cerr << buildLayout(probe.plan) << std::endl;
        std::cerr << "Performance Statistics:" << std::endl;
        std::cerr << "  Conversion time: " << duration.count() << " ms" << std::endl;
        std::cerr << "  Throughput: " << std::fixed << std::setprecision(2) << throughput_mb_s << " MB/s" << std::endl;
        std::cerr << "  Rows/second: " << std::fixed << std::setprecision(0) << rows_per_sec << " rows/s" << std::endl;
        std::cerr << "\nCompression Statistics:" << std::endl;
        std::cerr << "  Input CSV size: " << input_file_size << " bytes (" << std::fixed << std::setprecision(2) << (input_file_size / 1024.0) << " KB)" << std::endl;
        std::cerr << "  Output BCSV size: " << output_file_size << " bytes (" << std::fixed << std::setprecision(2) << (output_file_size / 1024.0) << " KB)" << std::endl;

        if (output_file_size <= input_file_size) {
            std::cerr << "  Compression ratio: " << std::fixed << std::setprecision(1) << compression_ratio << "%" << std::endl;
            std::cerr << "  Space saved: " << (input_file_size - output_file_size) << " bytes" << std::endl;
        } else {
            double size_increase_ratio = (static_cast<double>(output_file_size - input_file_size) / input_file_size) * 100.0;
            std::cerr << "  File size increase: " << std::fixed << std::setprecision(1) << size_increase_ratio << "% (overhead from binary format and metadata)" << std::endl;
            std::cerr << "  Additional space used: " << (output_file_size - input_file_size) << " bytes" << std::endl;
        }
        std::cerr << "  Compression mode: " << bcsv_cli::encodingDescription(
            config.row_codec, config.file_codec, config.compression_level) << std::endl;

        // --benchmark: structured timing output
        if (config.benchmark) {
            if (config.json_output) {
                // JSON blob to stderr (keeps stdout clean for piping)
                std::cerr << "{\"tool\":\"csv2bcsv\""
                          << ",\"input_file\":\"" << config.input_file << "\""
                          << ",\"output_file\":\"" << config.output_file << "\""
                          << ",\"rows\":" << row_count
                          << ",\"columns\":" << out_columns
                          << ",\"input_bytes\":" << input_file_size
                          << ",\"output_bytes\":" << output_file_size
                          << ",\"wall_ms\":" << duration_ms
                          << ",\"throughput_mb_s\":" << std::fixed << std::setprecision(2) << throughput_mb_s
                          << ",\"rows_per_sec\":" << std::fixed << std::setprecision(0) << rows_per_sec
                          << ",\"compression_ratio\":" << std::fixed << std::setprecision(1) << compression_ratio
                          << ",\"row_codec\":\"" << config.row_codec << "\""
                          << ",\"file_codec\":\"" << config.file_codec << "\""
                          << ",\"sample_rows\":" << config.sample_rows
                          << ",\"passes\":" << passes
                          << ",\"widened_columns\":" << widened_columns
                          << "}" << std::endl;
            } else {
                std::cerr << "[benchmark] csv2bcsv: "
                          << row_count << " rows, "
                          << duration_ms << " ms, "
                          << std::fixed << std::setprecision(2) << throughput_mb_s << " MB/s, "
                          << std::fixed << std::setprecision(0) << rows_per_sec << " rows/s\n";
            }
        }

    } catch (const ArgError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
