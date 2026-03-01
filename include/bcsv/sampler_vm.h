/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "sampler_compiler.h"
#include "sampler_ast.h"
#include "row.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <functional>
#include <stdexcept>
#include <array>
#include <unordered_map>

namespace bcsv {

    // ── VM Value ────────────────────────────────────────────────────
    //
    // Raw discriminated union — std::variant rejected for hot-loop
    // performance. The VM already knows the exact type of each stack
    // slot from bytecode compilation, so variant's safety overhead
    // provides no benefit.

    struct SamplerValue {
        enum class Tag : uint8_t { BOOL, INT, UINT, FLOAT, STRING };
        Tag tag;
        union {
            bool     b;
            int64_t  i;
            uint64_t u;
            double   f;
        };
        uint16_t string_idx = 0;  // valid when tag == STRING

        static SamplerValue makeBool(bool v)     { SamplerValue s; s.tag = Tag::BOOL;  s.b = v; return s; }
        static SamplerValue makeInt(int64_t v)   { SamplerValue s; s.tag = Tag::INT;   s.i = v; return s; }
        static SamplerValue makeUint(uint64_t v) { SamplerValue s; s.tag = Tag::UINT;  s.u = v; return s; }
        static SamplerValue makeFloat(double v)  { SamplerValue s; s.tag = Tag::FLOAT; s.f = v; return s; }
        static SamplerValue makeString(uint16_t idx) { SamplerValue s; s.tag = Tag::STRING; s.string_idx = idx; s.i = 0; return s; }
    };

    // ── Error Policy ────────────────────────────────────────────────

    enum class SamplerErrorPolicy : uint8_t {
        THROW,      // Throw std::runtime_error on runtime errors (div by zero)
        SKIP_ROW,   // Skip the current row (conditional → false, selection → skip)
        SATURATE,   // Replace with max/min value (div by zero → INT64_MAX / ±Inf)
    };

    // ── VM Result ───────────────────────────────────────────────────

    struct SamplerVMResult {
        bool     success    = true;
        bool     skip_row   = false;
        std::string error;
    };

    // ── Forward declarations ────────────────────────────────────────
    class Row;

    // ── Row Accessor ────────────────────────────────────────────────
    //
    // The VM needs a way to access rows by offset. This is provided
    // by the RowWindow (sampler_window.h), but the VM interface is
    // generic: any callable that returns a const Row& given an offset.

    using RowAccessor = std::function<const Row&(int16_t row_offset)>;

    // ── VM Class ────────────────────────────────────────────────────

    class SamplerVM {
    public:
        explicit SamplerVM(SamplerErrorPolicy policy = SamplerErrorPolicy::THROW)
            : policy_(policy) {}

        /// Evaluate a conditional bytecode program. Returns true if the row passes.
        template<typename Accessor>
        SamplerVMResult evalConditional(
            const SamplerBytecode& bc,
            const Accessor& rows,
            bool& result);

        /// Evaluate a selection bytecode program, writing output values to output_row.
        template<typename Accessor>
        SamplerVMResult evalSelection(
            const SamplerBytecode& bc,
            const Accessor& rows,
            Row& output_row);

    private:
        SamplerErrorPolicy policy_;

        // Value stack — fixed-size, typical depth 4–8
        static constexpr size_t MAX_STACK = 32;
        std::array<SamplerValue, MAX_STACK> stack_;
        size_t sp_ = 0;  // stack pointer (points to next free slot)

        // String working set — strings referenced during evaluation
        std::vector<std::string> strings_;
        // Dedup map: maps source string → index in strings_ (I4)
        std::unordered_map<std::string, uint16_t> string_dedup_;

        // ── Eval mode (I1) ──────────────────────────────────────────

        enum class EvalMode : uint8_t { CONDITIONAL, SELECTION };

        /// Unified dispatch loop — handles both conditional and selection
        template<EvalMode Mode, typename Accessor>
        SamplerVMResult dispatch(
            const SamplerBytecode& bc,
            const Accessor& rows,
            bool& cond_result,
            Row* output_row);

        // ── Stack operations (R1: overflow guard) ───────────────────

        void push(SamplerValue v) {
            if (sp_ >= MAX_STACK) [[unlikely]]
                throw std::runtime_error("Sampler: stack overflow (depth exceeds 32)");
            stack_[sp_++] = v;
        }
        SamplerValue pop() { return stack_[--sp_]; }
        SamplerValue& top() { return stack_[sp_ - 1]; }
        const SamplerValue& top() const { return stack_[sp_ - 1]; }

        // ── Inline read helpers ─────────────────────────────────────

        static int16_t readI16(const uint8_t* p) {
            uint16_t u = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
            int16_t v; std::memcpy(&v, &u, 2); return v;
        }

        static uint16_t readU16(const uint8_t* p) {
            return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
        }

        static int64_t readI64(const uint8_t* p) {
            int64_t v; std::memcpy(&v, p, 8); return v;
        }

        static uint64_t readU64(const uint8_t* p) {
            uint64_t v; std::memcpy(&v, p, 8); return v;
        }

        static double readF64(const uint8_t* p) {
            double v; std::memcpy(&v, p, 8); return v;
        }

        // ── Runtime error handling ──────────────────────────────────

        SamplerVMResult makeError(const std::string& msg) {
            SamplerVMResult r;
            r.success = false;
            r.error = msg;
            return r;
        }

        // ── String pool helpers (I4: dedup via hash map) ────────────

        uint16_t internString(const std::string& s) {
            auto it = string_dedup_.find(s);
            if (it != string_dedup_.end())
                return it->second;
            uint16_t idx = static_cast<uint16_t>(strings_.size());
            strings_.push_back(s);
            string_dedup_[s] = idx;
            return idx;
        }

        const std::string& getString(uint16_t idx, const SamplerBytecode& bc) const {
            // String indices < bc.string_pool.size() refer to constants
            if (idx < bc.string_pool.size())
                return bc.string_pool[idx];
            // Indices beyond refer to runtime strings
            return strings_[idx - bc.string_pool.size()];
        }
    };

} // namespace bcsv
