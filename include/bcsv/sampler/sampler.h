/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "sampler_ast.h"
#include "sampler_tokenizer.h"
#include "sampler_parser.h"
#include "sampler_types.h"
#include "sampler_compiler.h"
#include "sampler_vm.h"
#include "sampler_window.h"
#include "../reader.h"
#include "../layout.h"
#include "../row.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bcsv {

    // ── Sampler Mode ────────────────────────────────────────────────
    //
    // Mirrors BoundaryMode but uses the plan's public naming.

    enum class SamplerMode : uint8_t {
        TRUNCATE = 0,   // Skip rows where window is incomplete at boundaries
        EXPAND   = 1,   // Clamp out-of-bounds references to edge row
    };

    // ── Compile Result ──────────────────────────────────────────────

    struct SamplerCompileResult {
        bool        success         = true;
        std::string error_msg;           // Empty on success
        size_t      error_position  = 0; // Character offset in expression string
    };

    // ── Sampler ─────────────────────────────────────────────────────
    //
    // Streaming filter-and-project operator. Wraps a Reader and applies
    // a conditional (filter) and/or selection (projection) expression to
    // each row, backed by a bytecode VM and sliding window.
    //
    // Usage:
    //   Reader<Layout> reader;
    //   reader.open("data.bcsv");
    //   Sampler<Layout> sampler(reader);
    //   sampler.setConditional("X[0][0] != X[-1][0]");
    //   sampler.setSelection("X[0][0], X[0][1]");
    //   while (sampler.next()) {
    //       const auto& row = sampler.row();
    //       // use row...
    //   }

    template<LayoutConcept LayoutType = Layout>
    class Sampler {
    public:
        using RowType = typename LayoutType::RowType;

        // ── Construction ────────────────────────────────────────────

        explicit Sampler(Reader<LayoutType>& reader)
            : reader_(reader)
            , mode_(SamplerMode::TRUNCATE)
            , error_policy_(SamplerErrorPolicy::THROW)
            , has_conditional_(false)
            , has_selection_(false)
            , vm_(SamplerErrorPolicy::THROW)
            , source_row_pos_(0)
            , eof_(false)
        {}

        ~Sampler() = default;

        // Non-copyable, movable
        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;
        Sampler(Sampler&&) noexcept = default;
        Sampler& operator=(Sampler&&) noexcept = default;

        // ── Configuration ───────────────────────────────────────────

        SamplerCompileResult setConditional(std::string_view expr);
        const std::string& getConditional() const { return cond_expr_; }

        SamplerCompileResult setSelection(std::string_view expr);
        const std::string& getSelection() const { return sel_expr_; }

        void setMode(SamplerMode mode) { mode_ = mode; }
        SamplerMode getMode() const { return mode_; }

        void setErrorPolicy(SamplerErrorPolicy policy) {
            error_policy_ = policy;
            vm_ = SamplerVM(policy);
        }
        SamplerErrorPolicy getErrorPolicy() const { return error_policy_; }

        // ── Output Schema ───────────────────────────────────────────

        const Layout& outputLayout() const { return output_layout_; }

        // ── Iteration ───────────────────────────────────────────────

        bool next();
        const Row& row() const { return output_row_ ? *output_row_ : reader_.row(); }
        size_t sourceRowPos() const { return source_row_pos_; }

        // ── Bulk ────────────────────────────────────────────────────

        std::vector<Row> bulk() {
            std::vector<Row> result;
            while (next()) {
                result.push_back(row());
            }
            return result;
        }

        // ── Diagnostics ─────────────────────────────────────────────

        std::string disassemble() const {
            std::string out;
            if (has_conditional_) {
                out += "=== Conditional Bytecode ===\n";
                out += BytecodeCompiler<LayoutType>::disassemble(cond_bytecode_);
            }
            if (has_selection_) {
                out += "=== Selection Bytecode ===\n";
                out += BytecodeCompiler<LayoutType>::disassemble(sel_bytecode_);
            }
            return out;
        }

        size_t windowCapacity() const { return window_ ? window_->capacity() : 0; }

    private:
        Reader<LayoutType>&    reader_;
        SamplerMode            mode_;
        SamplerErrorPolicy     error_policy_;

        // Expression sources
        std::string            cond_expr_;
        std::string            sel_expr_;

        // Compiled state
        bool                   has_conditional_;
        bool                   has_selection_;
        SamplerBytecode        cond_bytecode_;
        SamplerBytecode        sel_bytecode_;

        // Per-expression offsets for R3 (recalculate on re-set)
        int16_t                cond_min_offset_ = 0;
        int16_t                cond_max_offset_ = 0;
        int16_t                sel_min_offset_  = 0;
        int16_t                sel_max_offset_  = 0;

        // Window
        std::unique_ptr<RowWindow> window_;
        int16_t                min_offset_ = 0;
        int16_t                max_offset_ = 0;

        // VM — reused across next() calls (B4)
        SamplerVM              vm_;

        // Output
        Layout                 output_layout_;
        std::unique_ptr<Row>   output_row_;

        // State
        size_t                 source_row_pos_;
        bool                   eof_;
        bool                   draining_ = false;  // B3: draining phase after EOF

        // ── Internal helpers ────────────────────────────────────────

        void rebuildWindow();
        bool evalCurrentRow();
        void recalculateOffsets();

        static void buildOutputLayout(const SelectionExpr& sel,
                                      const LayoutType& src_layout,
                                      Layout& out_layout);

        BoundaryMode toBoundaryMode() const {
            return (mode_ == SamplerMode::EXPAND) ? BoundaryMode::EXPAND : BoundaryMode::TRUNCATE;
        }
    };

} // namespace bcsv
