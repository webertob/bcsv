/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "sampler.h"
#include "sampler_vm.hpp"

#include <algorithm>
#include <cstring>

namespace bcsv {

    // ── setConditional ──────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    SamplerCompileResult Sampler<LayoutType>::setConditional(std::string_view expr) {
        SamplerCompileResult result;
        cond_expr_ = std::string(expr);

        // 1–2. Parse (tokenization is internal to parser)
        auto [ast, parse_result] = SamplerParser::parseConditional(expr);
        if (!parse_result.success) {
            result.success = false;
            result.error_msg = parse_result.error_msg;
            result.error_position = parse_result.error_position;
            has_conditional_ = false;
            return result;
        }

        // 3. Type-resolve
        TypeResolver<LayoutType> resolver(reader_.layout());
        auto resolve_result = resolver.resolve(*ast);
        if (!resolve_result.success) {
            result.success = false;
            result.error_msg = resolve_result.error_msg;
            has_conditional_ = false;
            return result;
        }

        // Store per-expression offsets (R3)
        cond_min_offset_ = resolve_result.min_offset;
        cond_max_offset_ = resolve_result.max_offset;

        // 4. Compile
        BytecodeCompiler<LayoutType> compiler(reader_.layout());
        cond_bytecode_ = compiler.compileConditional(*ast);
        has_conditional_ = true;

        // Detect fast-path: conditional is just "true"
        cond_passthrough_ = detectCondPassthrough(cond_bytecode_);

        // Recalculate merged offsets (must be after has_conditional_ = true)
        recalculateOffsets();

        // Rebuild window if needed
        rebuildWindow();

        return result;
    }

    // ── setSelection ────────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    SamplerCompileResult Sampler<LayoutType>::setSelection(std::string_view expr) {
        SamplerCompileResult result;
        sel_expr_ = std::string(expr);

        // 1–2. Parse (tokenization is internal to parser)
        auto sel_result = SamplerParser::parseSelection(expr);
        if (!sel_result.parse_result.success) {
            result.success = false;
            result.error_msg = sel_result.parse_result.error_msg;
            result.error_position = sel_result.parse_result.error_position;
            has_selection_ = false;
            return result;
        }

        auto& sel = sel_result.selection;

        // 3. Type-resolve
        TypeResolver<LayoutType> resolver(reader_.layout());
        auto resolve_result = resolver.resolveSelection(sel);
        if (!resolve_result.success) {
            result.success = false;
            result.error_msg = resolve_result.error_msg;
            has_selection_ = false;
            return result;
        }

        // Store per-expression offsets (R3)
        sel_min_offset_ = resolve_result.min_offset;
        sel_max_offset_ = resolve_result.max_offset;

        // 4. Compile
        BytecodeCompiler<LayoutType> compiler(reader_.layout());
        sel_bytecode_ = compiler.compileSelection(sel);
        has_selection_ = true;

        // Detect fast-path: selection copies all columns from X[0]
        sel_passthrough_ = detectSelPassthrough(
            sel_bytecode_, reader_.layout().columnCount());

        // Recalculate merged offsets (must be after has_selection_ = true)
        recalculateOffsets();

        // 5. Build output layout (I5: private static method)
        buildOutputLayout(sel, reader_.layout(), output_layout_);

        // Rebuild window
        rebuildWindow();

        return result;
    }

    // ── recalculateOffsets (R3) ─────────────────────────────────────

    template<LayoutConcept LayoutType>
    void Sampler<LayoutType>::recalculateOffsets() {
        min_offset_ = 0;
        max_offset_ = 0;
        if (has_conditional_) {
            min_offset_ = std::min(min_offset_, cond_min_offset_);
            max_offset_ = std::max(max_offset_, cond_max_offset_);
        }
        if (has_selection_) {
            min_offset_ = std::min(min_offset_, sel_min_offset_);
            max_offset_ = std::max(max_offset_, sel_max_offset_);
        }
    }

    // ── evalCurrentRow (helper for next) ────────────────────────────

    template<LayoutConcept LayoutType>
    bool Sampler<LayoutType>::evalCurrentRow() {
        // Evaluate conditional
        if (has_conditional_) {
            bool matches = false;
            SamplerVMResult vm_result;
            if (window_) {
                vm_result = vm_.evalConditional(cond_bytecode_,
                    [this](int16_t off) -> const Row& { return window_->resolve(off); },
                    matches);
            } else {
                const Row& cr = reader_.row();
                vm_result = vm_.evalConditional(cond_bytecode_,
                    [&cr](int16_t) -> const Row& { return cr; },
                    matches);
            }
            if (vm_result.skip_row) return false;
            if (!vm_result.success) { eof_ = true; return false; }
            if (!matches) return false;
        }

        // Evaluate selection (project)
        if (has_selection_ && output_row_) {
            SamplerVMResult vm_result;
            if (window_) {
                vm_result = vm_.evalSelection(sel_bytecode_,
                    [this](int16_t off) -> const Row& { return window_->resolve(off); },
                    *output_row_);
            } else {
                const Row& cr = reader_.row();
                vm_result = vm_.evalSelection(sel_bytecode_,
                    [&cr](int16_t) -> const Row& { return cr; },
                    *output_row_);
            }
            if (vm_result.skip_row) return false;
            if (!vm_result.success) { eof_ = true; return false; }
        }

        return true;
    }

    // ── next ────────────────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    bool Sampler<LayoutType>::next() {
        if (eof_) return false;

        // Effective passthrough: no conditional set, or conditional detected as always-true
        const bool cond_fast = !has_conditional_ || cond_passthrough_;
        const bool sel_fast  = !has_selection_  || sel_passthrough_;

        // Fast-path: both conditional and selection are passthrough.
        // Skip VM entirely — just read rows and return them directly.
        if (cond_fast && sel_fast && !window_) {
            if (!reader_.readNext()) {
                eof_ = true;
                return false;
            }
            source_row_pos_ = reader_.rowPos();
            // output_row_ not used — row() returns reader_.row()
            return true;
        }

        // Fast-path: conditional is always true (no filter), but
        // selection needs VM evaluation. Skip conditional VM call.
        if (cond_fast && !window_) {
            while (true) {
                if (!reader_.readNext()) {
                    eof_ = true;
                    return false;
                }
                source_row_pos_ = reader_.rowPos();

                if (has_selection_ && output_row_) {
                    const Row& cr = reader_.row();
                    auto vm_result = vm_.evalSelection(sel_bytecode_,
                        [&cr](int16_t) -> const Row& { return cr; },
                        *output_row_);
                    if (vm_result.skip_row) continue;
                    if (!vm_result.success) { eof_ = true; return false; }
                }
                return true;
            }
        }

        while (true) {
            // Handle draining phase (B3: rows after EOF with lookahead)
            if (draining_) {
                if (!window_ || !window_->hasDrains()) {
                    eof_ = true;
                    return false;
                }
                window_->advanceDrain();
                if (evalCurrentRow()) return true;
                continue;
            }

            // Read a row from the source
            if (!reader_.readNext()) {
                // Enter draining phase if EXPAND mode with lookahead (B3)
                if (window_ && window_->lookahead() > 0 &&
                    mode_ == SamplerMode::EXPAND) {
                    draining_ = true;
                    continue;  // will enter drain branch above
                }
                eof_ = true;
                return false;
            }

            // Push row into window
            if (window_) {
                window_->push(reader_.row());

                // Wait until window is ready (filling phase)
                if (!window_->ready()) continue;
            }

            source_row_pos_ = reader_.rowPos();

            if (evalCurrentRow()) return true;
        }
    }

    // ── rebuildWindow ───────────────────────────────────────────────

    template<LayoutConcept LayoutType>
    void Sampler<LayoutType>::rebuildWindow() {
        // Reset draining state
        draining_ = false;

        // Only need a window if there are non-zero offsets
        if (min_offset_ == 0 && max_offset_ == 0) {
            window_.reset();

            // Need output row for selection unless it's a passthrough
            if (has_selection_ && !sel_passthrough_ && !output_row_) {
                output_row_ = std::make_unique<Row>(output_layout_);
            }
            return;
        }

        // Create the layout for the window from the reader
        // We need a dynamic Layout for the window rows
        Layout src_layout = reader_.layout();
        window_ = std::make_unique<RowWindow>(
            src_layout, min_offset_, max_offset_, toBoundaryMode());

        if (has_selection_ && !output_row_) {
            output_row_ = std::make_unique<Row>(output_layout_);
        }
    }

    // ── buildOutputLayout (I5: private static method) ───────────────

    namespace detail {
        template<typename LT>
        inline std::string makeOutputName(const LT& src, uint16_t col, int16_t row_off) {
            std::string name;
            if (col < src.columnCount() && !src.columnName(col).empty()) {
                name = std::string(src.columnName(col));
            } else {
                name = "col" + std::to_string(col);
            }
            if (row_off < 0) name += "_m" + std::to_string(-row_off);
            else if (row_off > 0) name += "_p" + std::to_string(row_off);
            return name;
        }

        inline ColumnType exprTypeToColumnType(ExprType t) {
            switch (t) {
                case ExprType::BOOL:   return ColumnType::BOOL;
                case ExprType::INT:    return ColumnType::INT64;
                case ExprType::UINT:   return ColumnType::UINT64;
                case ExprType::FLOAT:  return ColumnType::DOUBLE;
                case ExprType::STRING: return ColumnType::STRING;
                default:               return ColumnType::DOUBLE;
            }
        }
    }

    template<LayoutConcept LayoutType>
    void Sampler<LayoutType>::buildOutputLayout(
        const SelectionExpr& sel, const LayoutType& src_layout, Layout& out_layout)
    {
        std::vector<ColumnType> types;
        std::vector<std::string> names;
        uint16_t idx = 0;

        for (const auto& item : sel.items) {
            if (!item) continue;

            if (item->is_row_ref()) {
                // Expand entire row
                auto& rr = item->as_row_ref();
                for (size_t c = 0; c < src_layout.columnCount(); ++c) {
                    types.push_back(src_layout.columnType(c));
                    names.push_back(detail::makeOutputName(src_layout, static_cast<uint16_t>(c), rr.row_offset));
                    ++idx;
                }
            } else if (item->is_cell_ref() && item->as_cell_ref().is_wildcard) {
                auto& cr = item->as_cell_ref();
                for (size_t c = 0; c < src_layout.columnCount(); ++c) {
                    types.push_back(src_layout.columnType(c));
                    names.push_back(detail::makeOutputName(src_layout, static_cast<uint16_t>(c), cr.row_offset));
                    ++idx;
                }
            } else if (item->is_cell_ref()) {
                auto& cr = item->as_cell_ref();
                types.push_back(src_layout.columnType(cr.col_index));
                names.push_back(detail::makeOutputName(src_layout, cr.col_index, cr.row_offset));
                ++idx;
            } else {
                // Arithmetic expression
                types.push_back(detail::exprTypeToColumnType(item->resolved_type));
                names.push_back("expr" + std::to_string(idx));
                ++idx;
            }
        }

        // Build the output layout
        out_layout = Layout();
        for (size_t i = 0; i < types.size(); ++i) {
            out_layout.addColumn(ColumnDefinition{names[i], types[i]});
        }
    }

    // ── detectCondPassthrough ───────────────────────────────────────
    //
    // A conditional is passthrough if the bytecode is exactly:
    //   CONST_BOOL true(1)  HALT_COND
    // This matches `setConditional("true")`.

    template<LayoutConcept LayoutType>
    bool Sampler<LayoutType>::detectCondPassthrough(const SamplerBytecode& bc) {
        if (bc.code.size() != 3) return false;
        return bc.code[0] == static_cast<uint8_t>(SamplerOpcode::CONST_BOOL)
            && bc.code[1] == 1
            && bc.code[2] == static_cast<uint8_t>(SamplerOpcode::HALT_COND);
    }

    // ── detectSelPassthrough ────────────────────────────────────────
    //
    // A selection is passthrough if the bytecode is a sequence of
    //   (LOAD_xxx row_off=0 col=i  EMIT out_col=i)  for i = 0..col_count-1
    // followed by HALT_SEL.
    // This matches `setSelection("X[0][*]")`.

    template<LayoutConcept LayoutType>
    bool Sampler<LayoutType>::detectSelPassthrough(
        const SamplerBytecode& bc, size_t col_count)
    {
        if (col_count == 0) return false;

        // Each column produces: opcode(1) + row_off(2) + col(2) + EMIT(1) + out_col(2) = 8 bytes
        // Plus HALT_SEL(1) at the end
        size_t expected_size = col_count * 8 + 1;
        if (bc.code.size() != expected_size) return false;

        size_t ip = 0;
        for (size_t c = 0; c < col_count; ++c) {
            // Skip the load opcode (we accept any LOAD_xxx)
            uint8_t op = bc.code[ip++];
            if (op > static_cast<uint8_t>(SamplerOpcode::LOAD_STRING))
                return false;

            // row_offset must be 0
            uint16_t ro_raw = static_cast<uint16_t>(bc.code[ip])
                            | (static_cast<uint16_t>(bc.code[ip + 1]) << 8);
            int16_t ro;
            std::memcpy(&ro, &ro_raw, 2);
            ip += 2;
            if (ro != 0) return false;

            // col index must equal c
            uint16_t col = static_cast<uint16_t>(bc.code[ip])
                         | (static_cast<uint16_t>(bc.code[ip + 1]) << 8);
            ip += 2;
            if (col != static_cast<uint16_t>(c)) return false;

            // EMIT opcode
            if (bc.code[ip++] != static_cast<uint8_t>(SamplerOpcode::EMIT))
                return false;

            // out_col must equal c
            uint16_t out_col = static_cast<uint16_t>(bc.code[ip])
                             | (static_cast<uint16_t>(bc.code[ip + 1]) << 8);
            ip += 2;
            if (out_col != static_cast<uint16_t>(c)) return false;
        }

        // HALT_SEL at end
        return ip < bc.code.size()
            && bc.code[ip] == static_cast<uint8_t>(SamplerOpcode::HALT_SEL);
    }

} // namespace bcsv
