/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include "sampler_vm.h"
#include "../row.h"
#include "../row.hpp"
#include "../definitions.h"

#include <cmath>
#include <limits>

namespace bcsv {

    // ── Thin wrappers ───────────────────────────────────────────────

    template<typename Accessor>
    SamplerVMResult SamplerVM::evalConditional(
        const SamplerBytecode& bc, const Accessor& rows, bool& result)
    {
        sp_ = 0;
        strings_.clear();
        string_dedup_.clear();
        return dispatch<EvalMode::CONDITIONAL>(bc, rows, result, nullptr);
    }

    template<typename Accessor>
    SamplerVMResult SamplerVM::evalSelection(
        const SamplerBytecode& bc, const Accessor& rows, Row& output_row)
    {
        sp_ = 0;
        strings_.clear();
        string_dedup_.clear();
        bool dummy = false;
        return dispatch<EvalMode::SELECTION>(bc, rows, dummy, &output_row);
    }

    // ── Unified dispatch loop ───────────────────────────────────────
    //
    // Uses computed goto (GCC/Clang) for ~20% faster opcode dispatch
    // by eliminating the central switch branch. Falls back to switch
    // on compilers that don't support labels-as-values (MSVC).

    template<SamplerVM::EvalMode Mode, typename Accessor>
    SamplerVMResult SamplerVM::dispatch(
        const SamplerBytecode& bc,
        const Accessor& rows,
        bool& cond_result,
        Row* output_row)
    {
        const uint8_t* code = bc.code.data();
        const uint8_t* ip = code;
        const uint8_t* end = code + bc.code.size();

#if defined(__GNUC__) || defined(__clang__)    // ── Computed-goto path ──

        // Suppress -Wpedantic for labels-as-values (GCC/Clang extension)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

        // Dispatch table — one label per opcode, order matches SamplerOpcode enum
        static const void* const dispatch_table[] = {
            &&L_LOAD_BOOL,      &&L_LOAD_INT8,      &&L_LOAD_INT16,
            &&L_LOAD_INT32,     &&L_LOAD_INT64,
            &&L_LOAD_UINT8,     &&L_LOAD_UINT16,    &&L_LOAD_UINT32,
            &&L_LOAD_UINT64,
            &&L_LOAD_FLOAT,     &&L_LOAD_DOUBLE,    &&L_LOAD_STRING,
            &&L_CONST_BOOL,     &&L_CONST_INT,      &&L_CONST_UINT,
            &&L_CONST_FLOAT,    &&L_CONST_STRING,
            &&L_ADD_INT,        &&L_ADD_FLOAT,
            &&L_SUB_INT,        &&L_SUB_FLOAT,
            &&L_MUL_INT,        &&L_MUL_FLOAT,
            &&L_DIV_INT,        &&L_DIV_FLOAT,
            &&L_MOD_INT,        &&L_NEG_INT,        &&L_NEG_FLOAT,
            &&L_BIT_AND,        &&L_BIT_OR,         &&L_BIT_XOR,
            &&L_BIT_NOT,        &&L_BIT_SHL,        &&L_BIT_SHR,
            &&L_PROMOTE_INT_TO_FLOAT, &&L_PROMOTE_UINT_TO_INT,
            &&L_PROMOTE_UINT_TO_FLOAT, &&L_PROMOTE_BOOL_TO_INT,
            &&L_CMP_EQ_INT,     &&L_CMP_EQ_FLOAT,  &&L_CMP_EQ_STRING,
            &&L_CMP_NE_INT,     &&L_CMP_NE_FLOAT,  &&L_CMP_NE_STRING,
            &&L_CMP_LT_INT,     &&L_CMP_LT_FLOAT,
            &&L_CMP_LE_INT,     &&L_CMP_LE_FLOAT,
            &&L_CMP_GT_INT,     &&L_CMP_GT_FLOAT,
            &&L_CMP_GE_INT,     &&L_CMP_GE_FLOAT,
            &&L_POP,            &&L_BOOL_NOT,       &&L_BOOL_AND,
            &&L_BOOL_OR,
            &&L_JUMP_IF_FALSE,  &&L_JUMP_IF_TRUE,
            &&L_INT_TO_BOOL,    &&L_UINT_TO_BOOL,   &&L_FLOAT_TO_BOOL,
            &&L_HALT_COND,      &&L_EMIT,           &&L_HALT_SEL,
        };

        #define VM_DISPATCH()                                       \
            do {                                                    \
                if (ip >= end) goto L_END;                          \
                uint8_t op_ = *ip++;                                \
                if (op_ >= sizeof(dispatch_table)/sizeof(void*))    \
                    goto L_END;                                     \
                goto *dispatch_table[op_];                          \
            } while (0)

        VM_DISPATCH();  // initial dispatch

        // ── Load instructions ───────────────────────────────────────
        L_LOAD_BOOL: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeBool(r.get<bool>(col)));
            VM_DISPATCH();
        }
        L_LOAD_INT8: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeInt(static_cast<int64_t>(r.get<int8_t>(col))));
            VM_DISPATCH();
        }
        L_LOAD_INT16: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeInt(static_cast<int64_t>(r.get<int16_t>(col))));
            VM_DISPATCH();
        }
        L_LOAD_INT32: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeInt(static_cast<int64_t>(r.get<int32_t>(col))));
            VM_DISPATCH();
        }
        L_LOAD_INT64: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeInt(r.get<int64_t>(col)));
            VM_DISPATCH();
        }
        L_LOAD_UINT8: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeUint(static_cast<uint64_t>(r.get<uint8_t>(col))));
            VM_DISPATCH();
        }
        L_LOAD_UINT16: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeUint(static_cast<uint64_t>(r.get<uint16_t>(col))));
            VM_DISPATCH();
        }
        L_LOAD_UINT32: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeUint(static_cast<uint64_t>(r.get<uint32_t>(col))));
            VM_DISPATCH();
        }
        L_LOAD_UINT64: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeUint(r.get<uint64_t>(col)));
            VM_DISPATCH();
        }
        L_LOAD_FLOAT: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeFloat(static_cast<double>(r.get<float>(col))));
            VM_DISPATCH();
        }
        L_LOAD_DOUBLE: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            push(SamplerValue::makeFloat(r.get<double>(col)));
            VM_DISPATCH();
        }
        L_LOAD_STRING: {
            int16_t ro = readI16(ip); ip += 2;
            uint16_t col = readU16(ip); ip += 2;
            const Row& r = rows(ro);
            const std::string& s = r.get<std::string>(col);
            uint16_t idx = static_cast<uint16_t>(bc.string_pool.size()) + internString(s);
            push(SamplerValue::makeString(idx));
            VM_DISPATCH();
        }

        // ── Constants ───────────────────────────────────────────────
        L_CONST_BOOL:
            push(SamplerValue::makeBool(*ip++ != 0));
            VM_DISPATCH();
        L_CONST_INT:
            push(SamplerValue::makeInt(readI64(ip))); ip += 8;
            VM_DISPATCH();
        L_CONST_UINT:
            push(SamplerValue::makeUint(readU64(ip))); ip += 8;
            VM_DISPATCH();
        L_CONST_FLOAT:
            push(SamplerValue::makeFloat(readF64(ip))); ip += 8;
            VM_DISPATCH();
        L_CONST_STRING: {
            uint16_t idx = readU16(ip); ip += 2;
            push(SamplerValue::makeString(idx));
            VM_DISPATCH();
        }

        // ── Arithmetic ──────────────────────────────────────────────
        L_ADD_INT:   { auto b = pop(); top().i += b.i; VM_DISPATCH(); }
        L_ADD_FLOAT: { auto b = pop(); top().f += b.f; VM_DISPATCH(); }
        L_SUB_INT:   { auto b = pop(); top().i -= b.i; VM_DISPATCH(); }
        L_SUB_FLOAT: { auto b = pop(); top().f -= b.f; VM_DISPATCH(); }
        L_MUL_INT:   { auto b = pop(); top().i *= b.i; VM_DISPATCH(); }
        L_MUL_FLOAT: { auto b = pop(); top().f *= b.f; VM_DISPATCH(); }
        L_DIV_INT: {
            auto b = pop(); auto& a = top();
            if (b.i == 0) {
                if (policy_ == SamplerErrorPolicy::SATURATE) {
                    a.i = (a.i >= 0) ? std::numeric_limits<int64_t>::max()
                                     : std::numeric_limits<int64_t>::min();
                } else if (policy_ == SamplerErrorPolicy::SKIP_ROW) {
                    if constexpr (Mode == EvalMode::CONDITIONAL) cond_result = false;
                    return SamplerVMResult{true, true, ""};
                } else {
                    throw std::runtime_error("Sampler: integer division by zero");
                }
            } else {
                a.i /= b.i;
            }
            VM_DISPATCH();
        }
        L_DIV_FLOAT: { auto b = pop(); top().f /= b.f; VM_DISPATCH(); }
        L_MOD_INT: {
            auto b = pop(); auto& a = top();
            if (b.i == 0) {
                if (policy_ == SamplerErrorPolicy::SATURATE) {
                    a.i = 0;
                } else if (policy_ == SamplerErrorPolicy::SKIP_ROW) {
                    if constexpr (Mode == EvalMode::CONDITIONAL) cond_result = false;
                    return SamplerVMResult{true, true, ""};
                } else {
                    throw std::runtime_error("Sampler: integer modulo by zero");
                }
            } else {
                a.i %= b.i;
            }
            VM_DISPATCH();
        }
        L_NEG_INT:   top().i = -top().i; VM_DISPATCH();
        L_NEG_FLOAT: top().f = -top().f; VM_DISPATCH();

        // ── Bitwise ─────────────────────────────────────────────────
        L_BIT_AND: { auto b = pop(); top().i &= b.i; VM_DISPATCH(); }
        L_BIT_OR:  { auto b = pop(); top().i |= b.i; VM_DISPATCH(); }
        L_BIT_XOR: { auto b = pop(); top().i ^= b.i; VM_DISPATCH(); }
        L_BIT_NOT: top().i = ~top().i; VM_DISPATCH();
        L_BIT_SHL: {
            auto b = pop();
            int64_t shift = (b.i < 0) ? 0 : (b.i > 63) ? 63 : b.i;
            top().i <<= shift;
            VM_DISPATCH();
        }
        L_BIT_SHR: {
            auto b = pop();
            int64_t shift = (b.i < 0) ? 0 : (b.i > 63) ? 63 : b.i;
            top().i >>= shift;
            VM_DISPATCH();
        }

        // ── Promotions ──────────────────────────────────────────────
        L_PROMOTE_INT_TO_FLOAT: {
            auto& v = top(); v.f = static_cast<double>(v.i);
            v.tag = SamplerValue::Tag::FLOAT; VM_DISPATCH();
        }
        L_PROMOTE_UINT_TO_INT: {
            auto& v = top(); v.i = static_cast<int64_t>(v.u);
            v.tag = SamplerValue::Tag::INT; VM_DISPATCH();
        }
        L_PROMOTE_UINT_TO_FLOAT: {
            auto& v = top(); v.f = static_cast<double>(v.u);
            v.tag = SamplerValue::Tag::FLOAT; VM_DISPATCH();
        }
        L_PROMOTE_BOOL_TO_INT: {
            auto& v = top(); v.i = v.b ? 1 : 0;
            v.tag = SamplerValue::Tag::INT; VM_DISPATCH();
        }

        // ── Comparisons ─────────────────────────────────────────────
        L_CMP_EQ_INT:   { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.i == b.i); VM_DISPATCH(); }
        L_CMP_EQ_FLOAT: { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.f == b.f); VM_DISPATCH(); }
        L_CMP_EQ_STRING: {
            auto b = pop(); auto a_copy = pop();
            push(SamplerValue::makeBool(getString(a_copy.string_idx, bc) == getString(b.string_idx, bc)));
            VM_DISPATCH();
        }
        L_CMP_NE_INT:   { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.i != b.i); VM_DISPATCH(); }
        L_CMP_NE_FLOAT: { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.f != b.f); VM_DISPATCH(); }
        L_CMP_NE_STRING: {
            auto b = pop(); auto a_copy = pop();
            push(SamplerValue::makeBool(getString(a_copy.string_idx, bc) != getString(b.string_idx, bc)));
            VM_DISPATCH();
        }
        L_CMP_LT_INT:   { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.i < b.i);  VM_DISPATCH(); }
        L_CMP_LT_FLOAT: { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.f < b.f);  VM_DISPATCH(); }
        L_CMP_LE_INT:   { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.i <= b.i); VM_DISPATCH(); }
        L_CMP_LE_FLOAT: { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.f <= b.f); VM_DISPATCH(); }
        L_CMP_GT_INT:   { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.i > b.i);  VM_DISPATCH(); }
        L_CMP_GT_FLOAT: { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.f > b.f);  VM_DISPATCH(); }
        L_CMP_GE_INT:   { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.i >= b.i); VM_DISPATCH(); }
        L_CMP_GE_FLOAT: { auto b = pop(); auto& a = top(); a = SamplerValue::makeBool(a.f >= b.f); VM_DISPATCH(); }

        // ── Boolean / Control ───────────────────────────────────────
        L_POP:      pop(); VM_DISPATCH();
        L_BOOL_NOT: top().b = !top().b; VM_DISPATCH();
        L_BOOL_AND: { auto b = pop(); top().b = top().b && b.b; VM_DISPATCH(); }
        L_BOOL_OR:  { auto b = pop(); top().b = top().b || b.b; VM_DISPATCH(); }
        L_JUMP_IF_FALSE: {
            int16_t offset = readI16(ip); ip += 2;
            if (!top().b) ip += offset;
            VM_DISPATCH();
        }
        L_JUMP_IF_TRUE: {
            int16_t offset = readI16(ip); ip += 2;
            if (top().b) ip += offset;
            VM_DISPATCH();
        }

        // ── Implicit bool conversions ───────────────────────────────
        L_INT_TO_BOOL: {
            auto& v = top(); v.b = v.i != 0;
            v.tag = SamplerValue::Tag::BOOL; VM_DISPATCH();
        }
        L_UINT_TO_BOOL: {
            auto& v = top(); v.b = v.u != 0;
            v.tag = SamplerValue::Tag::BOOL; VM_DISPATCH();
        }
        L_FLOAT_TO_BOOL: {
            auto& v = top(); v.b = v.f != 0.0;
            v.tag = SamplerValue::Tag::BOOL; VM_DISPATCH();
        }

        // ── Terminators ─────────────────────────────────────────────
        L_HALT_COND: {
            if constexpr (Mode == EvalMode::CONDITIONAL) {
                cond_result = top().b;
                sp_ = 0;
                return SamplerVMResult{};
            } else {
                return makeError("Sampler: HALT_COND in selection bytecode");
            }
        }
        L_EMIT: {
            if constexpr (Mode == EvalMode::SELECTION) {
                uint16_t out_col = readU16(ip); ip += 2;
                auto val = pop();
                ColumnType ct = output_row->layout().columnType(out_col);
                switch (ct) {
                    case ColumnType::BOOL:   output_row->set<bool>(out_col, val.b); break;
                    case ColumnType::INT8:   output_row->set<int8_t>(out_col, static_cast<int8_t>(val.i)); break;
                    case ColumnType::INT16:  output_row->set<int16_t>(out_col, static_cast<int16_t>(val.i)); break;
                    case ColumnType::INT32:  output_row->set<int32_t>(out_col, static_cast<int32_t>(val.i)); break;
                    case ColumnType::INT64:  output_row->set<int64_t>(out_col, val.i); break;
                    case ColumnType::UINT8:  output_row->set<uint8_t>(out_col, static_cast<uint8_t>(val.u)); break;
                    case ColumnType::UINT16: output_row->set<uint16_t>(out_col, static_cast<uint16_t>(val.u)); break;
                    case ColumnType::UINT32: output_row->set<uint32_t>(out_col, static_cast<uint32_t>(val.u)); break;
                    case ColumnType::UINT64: output_row->set<uint64_t>(out_col, val.u); break;
                    case ColumnType::FLOAT:  output_row->set<float>(out_col, static_cast<float>(val.f)); break;
                    case ColumnType::DOUBLE: output_row->set<double>(out_col, val.f); break;
                    case ColumnType::STRING: output_row->set<std::string>(out_col, getString(val.string_idx, bc)); break;
                    default: break;
                }
                VM_DISPATCH();
            } else {
                return makeError("Sampler: EMIT/HALT_SEL in conditional bytecode");
            }
        }
        L_HALT_SEL: {
            if constexpr (Mode == EvalMode::SELECTION) {
                sp_ = 0;
                return SamplerVMResult{};
            } else {
                return makeError("Sampler: HALT_SEL in conditional bytecode");
            }
        }

        L_END: ;  // fell off end of bytecode

        #undef VM_DISPATCH

#pragma GCC diagnostic pop

#else  // ── Switch-based fallback (MSVC, etc.) ──

        while (ip < end) {
            auto op = static_cast<SamplerOpcode>(*ip++);

            switch (op) {

            // ── Load instructions ───────────────────────────────────
            case SamplerOpcode::LOAD_BOOL: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeBool(r.get<bool>(col)));
                break;
            }
            case SamplerOpcode::LOAD_INT8: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeInt(static_cast<int64_t>(r.get<int8_t>(col))));
                break;
            }
            case SamplerOpcode::LOAD_INT16: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeInt(static_cast<int64_t>(r.get<int16_t>(col))));
                break;
            }
            case SamplerOpcode::LOAD_INT32: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeInt(static_cast<int64_t>(r.get<int32_t>(col))));
                break;
            }
            case SamplerOpcode::LOAD_INT64: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeInt(r.get<int64_t>(col)));
                break;
            }
            case SamplerOpcode::LOAD_UINT8: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeUint(static_cast<uint64_t>(r.get<uint8_t>(col))));
                break;
            }
            case SamplerOpcode::LOAD_UINT16: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeUint(static_cast<uint64_t>(r.get<uint16_t>(col))));
                break;
            }
            case SamplerOpcode::LOAD_UINT32: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeUint(static_cast<uint64_t>(r.get<uint32_t>(col))));
                break;
            }
            case SamplerOpcode::LOAD_UINT64: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeUint(r.get<uint64_t>(col)));
                break;
            }
            case SamplerOpcode::LOAD_FLOAT: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeFloat(static_cast<double>(r.get<float>(col))));
                break;
            }
            case SamplerOpcode::LOAD_DOUBLE: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                push(SamplerValue::makeFloat(r.get<double>(col)));
                break;
            }
            case SamplerOpcode::LOAD_STRING: {
                int16_t ro = readI16(ip); ip += 2;
                uint16_t col = readU16(ip); ip += 2;
                const Row& r = rows(ro);
                const std::string& s = r.get<std::string>(col);
                uint16_t idx = static_cast<uint16_t>(bc.string_pool.size()) + internString(s);
                push(SamplerValue::makeString(idx));
                break;
            }

            // ── Constants ───────────────────────────────────────────
            case SamplerOpcode::CONST_BOOL:
                push(SamplerValue::makeBool(*ip++ != 0));
                break;
            case SamplerOpcode::CONST_INT:
                push(SamplerValue::makeInt(readI64(ip))); ip += 8;
                break;
            case SamplerOpcode::CONST_UINT:
                push(SamplerValue::makeUint(readU64(ip))); ip += 8;
                break;
            case SamplerOpcode::CONST_FLOAT:
                push(SamplerValue::makeFloat(readF64(ip))); ip += 8;
                break;
            case SamplerOpcode::CONST_STRING: {
                uint16_t idx = readU16(ip); ip += 2;
                push(SamplerValue::makeString(idx));
                break;
            }

            // ── Arithmetic ──────────────────────────────────────────
            case SamplerOpcode::ADD_INT: {
                auto b = pop(); top().i += b.i; break;
            }
            case SamplerOpcode::ADD_FLOAT: {
                auto b = pop(); top().f += b.f; break;
            }
            case SamplerOpcode::SUB_INT: {
                auto b = pop(); top().i -= b.i; break;
            }
            case SamplerOpcode::SUB_FLOAT: {
                auto b = pop(); top().f -= b.f; break;
            }
            case SamplerOpcode::MUL_INT: {
                auto b = pop(); top().i *= b.i; break;
            }
            case SamplerOpcode::MUL_FLOAT: {
                auto b = pop(); top().f *= b.f; break;
            }
            case SamplerOpcode::DIV_INT: {
                auto b = pop(); auto& a = top();
                if (b.i == 0) {
                    if (policy_ == SamplerErrorPolicy::SATURATE) {
                        a.i = (a.i >= 0) ? std::numeric_limits<int64_t>::max()
                                         : std::numeric_limits<int64_t>::min();
                    } else if (policy_ == SamplerErrorPolicy::SKIP_ROW) {
                        if constexpr (Mode == EvalMode::CONDITIONAL) cond_result = false;
                        return SamplerVMResult{true, true, ""};
                    } else {
                        throw std::runtime_error("Sampler: integer division by zero");
                    }
                } else {
                    a.i /= b.i;
                }
                break;
            }
            case SamplerOpcode::DIV_FLOAT: {
                auto b = pop(); top().f /= b.f;
                break;
            }
            case SamplerOpcode::MOD_INT: {
                auto b = pop(); auto& a = top();
                if (b.i == 0) {
                    if (policy_ == SamplerErrorPolicy::SATURATE) {
                        a.i = 0;
                    } else if (policy_ == SamplerErrorPolicy::SKIP_ROW) {
                        if constexpr (Mode == EvalMode::CONDITIONAL) cond_result = false;
                        return SamplerVMResult{true, true, ""};
                    } else {
                        throw std::runtime_error("Sampler: integer modulo by zero");
                    }
                } else {
                    a.i %= b.i;
                }
                break;
            }
            case SamplerOpcode::NEG_INT:
                top().i = -top().i; break;
            case SamplerOpcode::NEG_FLOAT:
                top().f = -top().f; break;

            // ── Bitwise ─────────────────────────────────────────────
            case SamplerOpcode::BIT_AND: {
                auto b = pop(); top().i &= b.i; break;
            }
            case SamplerOpcode::BIT_OR: {
                auto b = pop(); top().i |= b.i; break;
            }
            case SamplerOpcode::BIT_XOR: {
                auto b = pop(); top().i ^= b.i; break;
            }
            case SamplerOpcode::BIT_NOT:
                top().i = ~top().i; break;
            case SamplerOpcode::BIT_SHL: {
                auto b = pop();
                int64_t shift = (b.i < 0) ? 0 : (b.i > 63) ? 63 : b.i;
                top().i <<= shift; break;
            }
            case SamplerOpcode::BIT_SHR: {
                auto b = pop();
                int64_t shift = (b.i < 0) ? 0 : (b.i > 63) ? 63 : b.i;
                top().i >>= shift; break;
            }

            // ── Promotions ──────────────────────────────────────────
            case SamplerOpcode::PROMOTE_INT_TO_FLOAT: {
                auto& v = top();
                v.f = static_cast<double>(v.i);
                v.tag = SamplerValue::Tag::FLOAT;
                break;
            }
            case SamplerOpcode::PROMOTE_UINT_TO_INT: {
                auto& v = top();
                v.i = static_cast<int64_t>(v.u);
                v.tag = SamplerValue::Tag::INT;
                break;
            }
            case SamplerOpcode::PROMOTE_UINT_TO_FLOAT: {
                auto& v = top();
                v.f = static_cast<double>(v.u);
                v.tag = SamplerValue::Tag::FLOAT;
                break;
            }
            case SamplerOpcode::PROMOTE_BOOL_TO_INT: {
                auto& v = top();
                v.i = v.b ? 1 : 0;
                v.tag = SamplerValue::Tag::INT;
                break;
            }

            // ── Comparisons ─────────────────────────────────────────
            case SamplerOpcode::CMP_EQ_INT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.i == b.i); break;
            }
            case SamplerOpcode::CMP_EQ_FLOAT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.f == b.f); break;
            }
            case SamplerOpcode::CMP_EQ_STRING: {
                auto b = pop(); auto a_copy = pop();
                push(SamplerValue::makeBool(
                    getString(a_copy.string_idx, bc) == getString(b.string_idx, bc)));
                break;
            }
            case SamplerOpcode::CMP_NE_INT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.i != b.i); break;
            }
            case SamplerOpcode::CMP_NE_FLOAT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.f != b.f); break;
            }
            case SamplerOpcode::CMP_NE_STRING: {
                auto b = pop(); auto a_copy = pop();
                push(SamplerValue::makeBool(
                    getString(a_copy.string_idx, bc) != getString(b.string_idx, bc)));
                break;
            }
            case SamplerOpcode::CMP_LT_INT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.i < b.i); break;
            }
            case SamplerOpcode::CMP_LT_FLOAT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.f < b.f); break;
            }
            case SamplerOpcode::CMP_LE_INT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.i <= b.i); break;
            }
            case SamplerOpcode::CMP_LE_FLOAT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.f <= b.f); break;
            }
            case SamplerOpcode::CMP_GT_INT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.i > b.i); break;
            }
            case SamplerOpcode::CMP_GT_FLOAT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.f > b.f); break;
            }
            case SamplerOpcode::CMP_GE_INT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.i >= b.i); break;
            }
            case SamplerOpcode::CMP_GE_FLOAT: {
                auto b = pop(); auto& a = top();
                a = SamplerValue::makeBool(a.f >= b.f); break;
            }

            // ── Boolean / Control ───────────────────────────────────
            case SamplerOpcode::POP:
                pop(); break;
            case SamplerOpcode::BOOL_NOT:
                top().b = !top().b; break;
            case SamplerOpcode::BOOL_AND: {
                auto b = pop(); top().b = top().b && b.b; break;
            }
            case SamplerOpcode::BOOL_OR: {
                auto b = pop(); top().b = top().b || b.b; break;
            }
            case SamplerOpcode::JUMP_IF_FALSE: {
                int16_t offset = readI16(ip); ip += 2;
                if (!top().b) ip += offset;
                break;
            }
            case SamplerOpcode::JUMP_IF_TRUE: {
                int16_t offset = readI16(ip); ip += 2;
                if (top().b) ip += offset;
                break;
            }

            // ── Implicit bool conversions ───────────────────────────
            case SamplerOpcode::INT_TO_BOOL: {
                auto& v = top();
                v.b = v.i != 0;
                v.tag = SamplerValue::Tag::BOOL;
                break;
            }
            case SamplerOpcode::UINT_TO_BOOL: {
                auto& v = top();
                v.b = v.u != 0;
                v.tag = SamplerValue::Tag::BOOL;
                break;
            }
            case SamplerOpcode::FLOAT_TO_BOOL: {
                auto& v = top();
                v.b = v.f != 0.0;
                v.tag = SamplerValue::Tag::BOOL;
                break;
            }

            // ── Terminators ─────────────────────────────────────────
            case SamplerOpcode::HALT_COND: {
                if constexpr (Mode == EvalMode::CONDITIONAL) {
                    cond_result = top().b;
                    sp_ = 0;
                    return SamplerVMResult{};
                } else {
                    return makeError("Sampler: HALT_COND in selection bytecode");
                }
            }

            case SamplerOpcode::EMIT: {
                if constexpr (Mode == EvalMode::SELECTION) {
                    uint16_t out_col = readU16(ip); ip += 2;
                    auto val = pop();
                    ColumnType ct = output_row->layout().columnType(out_col);
                    switch (ct) {
                        case ColumnType::BOOL:   output_row->set<bool>(out_col, val.b); break;
                        case ColumnType::INT8:   output_row->set<int8_t>(out_col, static_cast<int8_t>(val.i)); break;
                        case ColumnType::INT16:  output_row->set<int16_t>(out_col, static_cast<int16_t>(val.i)); break;
                        case ColumnType::INT32:  output_row->set<int32_t>(out_col, static_cast<int32_t>(val.i)); break;
                        case ColumnType::INT64:  output_row->set<int64_t>(out_col, val.i); break;
                        case ColumnType::UINT8:  output_row->set<uint8_t>(out_col, static_cast<uint8_t>(val.u)); break;
                        case ColumnType::UINT16: output_row->set<uint16_t>(out_col, static_cast<uint16_t>(val.u)); break;
                        case ColumnType::UINT32: output_row->set<uint32_t>(out_col, static_cast<uint32_t>(val.u)); break;
                        case ColumnType::UINT64: output_row->set<uint64_t>(out_col, val.u); break;
                        case ColumnType::FLOAT:  output_row->set<float>(out_col, static_cast<float>(val.f)); break;
                        case ColumnType::DOUBLE: output_row->set<double>(out_col, val.f); break;
                        case ColumnType::STRING: output_row->set<std::string>(out_col, getString(val.string_idx, bc)); break;
                        default: break;
                    }
                } else {
                    return makeError("Sampler: EMIT/HALT_SEL in conditional bytecode");
                }
                break;
            }

            case SamplerOpcode::HALT_SEL: {
                if constexpr (Mode == EvalMode::SELECTION) {
                    sp_ = 0;
                    return SamplerVMResult{};
                } else {
                    return makeError("Sampler: HALT_SEL in conditional bytecode");
                }
            }

            } // switch
        } // while

#endif  // computed-goto vs switch

        if constexpr (Mode == EvalMode::CONDITIONAL)
            return makeError("Sampler: conditional bytecode did not terminate with HALT_COND");
        else
            return makeError("Sampler: selection bytecode did not terminate with HALT_SEL");
    }

} // namespace bcsv
