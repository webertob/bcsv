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
#include "sampler_types.h"
#include "../definitions.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <variant>
#include <vector>

namespace bcsv {

    // ── Opcodes ─────────────────────────────────────────────────────

    enum class SamplerOpcode : uint8_t {
        // Load (type-specialised)
        LOAD_BOOL,   LOAD_INT8,   LOAD_INT16,  LOAD_INT32,  LOAD_INT64,
        LOAD_UINT8,  LOAD_UINT16, LOAD_UINT32, LOAD_UINT64,
        LOAD_FLOAT,  LOAD_DOUBLE, LOAD_STRING,

        // Constants
        CONST_BOOL,  CONST_INT,   CONST_UINT,  CONST_FLOAT, CONST_STRING,

        // Arithmetic
        ADD_INT,     ADD_FLOAT,
        SUB_INT,     SUB_FLOAT,
        MUL_INT,     MUL_FLOAT,
        DIV_INT,     DIV_FLOAT,
        MOD_INT,
        NEG_INT,     NEG_FLOAT,

        // Bitwise
        BIT_AND,     BIT_OR,      BIT_XOR,     BIT_NOT,
        BIT_SHL,     BIT_SHR,

        // Promotions
        PROMOTE_INT_TO_FLOAT,
        PROMOTE_UINT_TO_INT,
        PROMOTE_UINT_TO_FLOAT,
        PROMOTE_BOOL_TO_INT,

        // Comparisons
        CMP_EQ_INT,  CMP_EQ_FLOAT,  CMP_EQ_STRING,
        CMP_NE_INT,  CMP_NE_FLOAT,  CMP_NE_STRING,
        CMP_LT_INT,  CMP_LT_FLOAT,
        CMP_LE_INT,  CMP_LE_FLOAT,
        CMP_GT_INT,  CMP_GT_FLOAT,
        CMP_GE_INT,  CMP_GE_FLOAT,

        // Boolean / Control
        POP,
        BOOL_NOT,
        BOOL_AND,
        BOOL_OR,
        JUMP_IF_FALSE,   // peek + conditional jump (i16 offset)
        JUMP_IF_TRUE,    // peek + conditional jump (i16 offset)

        // Implicit bool conversions (numeric != 0)
        INT_TO_BOOL,
        UINT_TO_BOOL,
        FLOAT_TO_BOOL,

        // Terminators
        HALT_COND,       // pop top as bool → conditional result
        EMIT,            // pop top → write to output column (u16 index)
        HALT_SEL,        // selection program complete
    };

    // ── Bytecode Builder ────────────────────────────────────────────

    /// Compiled bytecode program (instruction stream + string constant pool)
    struct SamplerBytecode {
        std::vector<uint8_t>     code;
        std::vector<std::string> string_pool;
    };

    // ── Bytecode Compiler ───────────────────────────────────────────
    //
    // Compiles a type-resolved AST into a SamplerBytecode program.
    // The AST must have been through TypeResolver first (all nodes
    // have resolved_type set, all CellRefs have col_index resolved).

    template<typename LayoutType>
    class BytecodeCompiler {
    public:
        explicit BytecodeCompiler(const LayoutType& layout)
            : layout_(layout) {}

        /// Compile a conditional expression AST → bytecode ending with HALT_COND
        SamplerBytecode compileConditional(const AstNode& root) {
            bc_ = {};
            compileNode(root);
            // Ensure top of stack is bool
            ensureBool(root);
            emit(SamplerOpcode::HALT_COND);
            return std::move(bc_);
        }

        /// Compile a selection expression → bytecode ending with HALT_SEL
        SamplerBytecode compileSelection(const SelectionExpr& sel) {
            bc_ = {};
            uint16_t out_col = 0;

            for (const auto& item : sel.items) {
                if (!item) continue;

                // Handle wildcard/row-ref expansion
                if (item->is_row_ref()) {
                    auto& rr = item->as_row_ref();
                    for (size_t c = 0; c < layout_.columnCount(); ++c) {
                        emitLoad(rr.row_offset, static_cast<uint16_t>(c),
                                 layout_.columnType(c));
                        emitOp(SamplerOpcode::EMIT);
                        emitU16(out_col++);
                    }
                } else if (item->is_cell_ref() && item->as_cell_ref().is_wildcard) {
                    auto& cr = item->as_cell_ref();
                    for (size_t c = 0; c < layout_.columnCount(); ++c) {
                        emitLoad(cr.row_offset, static_cast<uint16_t>(c),
                                 layout_.columnType(c));
                        emitOp(SamplerOpcode::EMIT);
                        emitU16(out_col++);
                    }
                } else {
                    compileNode(*item);
                    emitOp(SamplerOpcode::EMIT);
                    emitU16(out_col++);
                }
            }

            emit(SamplerOpcode::HALT_SEL);
            return std::move(bc_);
        }

        /// Disassemble bytecode to human-readable string
        static std::string disassemble(const SamplerBytecode& bc) {
            std::ostringstream out;
            size_t ip = 0;
            while (ip < bc.code.size()) {
                out << std::to_string(ip) << ": ";
                auto op = static_cast<SamplerOpcode>(bc.code[ip++]);
                out << opcodeName(op);

                switch (op) {
                    // Load: row_off:i16, col:u16
                    case SamplerOpcode::LOAD_BOOL:
                    case SamplerOpcode::LOAD_INT8:
                    case SamplerOpcode::LOAD_INT16:
                    case SamplerOpcode::LOAD_INT32:
                    case SamplerOpcode::LOAD_INT64:
                    case SamplerOpcode::LOAD_UINT8:
                    case SamplerOpcode::LOAD_UINT16:
                    case SamplerOpcode::LOAD_UINT32:
                    case SamplerOpcode::LOAD_UINT64:
                    case SamplerOpcode::LOAD_FLOAT:
                    case SamplerOpcode::LOAD_DOUBLE:
                    case SamplerOpcode::LOAD_STRING: {
                        int16_t ro = readI16(bc.code, ip); ip += 2;
                        uint16_t col = readU16(bc.code, ip); ip += 2;
                        out << " row_off=" << ro << " col=" << col;
                        break;
                    }

                    case SamplerOpcode::CONST_BOOL: {
                        out << " " << (bc.code[ip++] ? "true" : "false");
                        break;
                    }
                    case SamplerOpcode::CONST_INT: {
                        int64_t v; std::memcpy(&v, &bc.code[ip], 8); ip += 8;
                        out << " " << v;
                        break;
                    }
                    case SamplerOpcode::CONST_UINT: {
                        uint64_t v; std::memcpy(&v, &bc.code[ip], 8); ip += 8;
                        out << " " << v;
                        break;
                    }
                    case SamplerOpcode::CONST_FLOAT: {
                        double v; std::memcpy(&v, &bc.code[ip], 8); ip += 8;
                        out << " " << v;
                        break;
                    }
                    case SamplerOpcode::CONST_STRING: {
                        uint16_t idx = readU16(bc.code, ip); ip += 2;
                        if (idx < bc.string_pool.size())
                            out << " \"" << bc.string_pool[idx] << "\"";
                        else
                            out << " string_idx=" << idx;
                        break;
                    }

                    case SamplerOpcode::JUMP_IF_FALSE:
                    case SamplerOpcode::JUMP_IF_TRUE: {
                        int16_t off = readI16(bc.code, ip); ip += 2;
                        out << " offset=" << off << " (target=" << (ip + off) << ")";
                        break;
                    }

                    case SamplerOpcode::EMIT: {
                        uint16_t col = readU16(bc.code, ip); ip += 2;
                        out << " out_col=" << col;
                        break;
                    }

                    default:
                        break;
                }
                out << "\n";
            }
            return out.str();
        }

    private:
        const LayoutType& layout_;
        SamplerBytecode   bc_;

        // ── Emit helpers ────────────────────────────────────────────

        void emit(SamplerOpcode op) { bc_.code.push_back(static_cast<uint8_t>(op)); }
        void emitOp(SamplerOpcode op) { emit(op); }

        void emitU8(uint8_t v) { bc_.code.push_back(v); }

        void emitU16(uint16_t v) {
            bc_.code.push_back(static_cast<uint8_t>(v & 0xFF));
            bc_.code.push_back(static_cast<uint8_t>(v >> 8));
        }

        void emitI16(int16_t v) {
            uint16_t u;
            std::memcpy(&u, &v, 2);
            emitU16(u);
        }

        void emitI64(int64_t v) {
            size_t pos = bc_.code.size();
            bc_.code.resize(pos + 8);
            std::memcpy(&bc_.code[pos], &v, 8);
        }

        void emitU64(uint64_t v) {
            size_t pos = bc_.code.size();
            bc_.code.resize(pos + 8);
            std::memcpy(&bc_.code[pos], &v, 8);
        }

        void emitF64(double v) {
            size_t pos = bc_.code.size();
            bc_.code.resize(pos + 8);
            std::memcpy(&bc_.code[pos], &v, 8);
        }

        /// Returns the current code offset
        size_t codePos() const { return bc_.code.size(); }

        /// Patch a 16-bit value at a given code position
        void patchI16(size_t pos, int16_t v) {
            uint16_t u;
            std::memcpy(&u, &v, 2);
            bc_.code[pos]     = static_cast<uint8_t>(u & 0xFF);
            bc_.code[pos + 1] = static_cast<uint8_t>(u >> 8);
        }

        static int16_t readI16(const std::vector<uint8_t>& code, size_t pos) {
            uint16_t u = static_cast<uint16_t>(code[pos]) |
                         (static_cast<uint16_t>(code[pos + 1]) << 8);
            int16_t v;
            std::memcpy(&v, &u, 2);
            return v;
        }

        static uint16_t readU16(const std::vector<uint8_t>& code, size_t pos) {
            return static_cast<uint16_t>(code[pos]) |
                   (static_cast<uint16_t>(code[pos + 1]) << 8);
        }

        // ── Type-specific load ──────────────────────────────────────

        void emitLoad(int16_t row_off, uint16_t col, ColumnType ct) {
            SamplerOpcode op;
            switch (ct) {
                case ColumnType::BOOL:   op = SamplerOpcode::LOAD_BOOL;   break;
                case ColumnType::INT8:   op = SamplerOpcode::LOAD_INT8;   break;
                case ColumnType::INT16:  op = SamplerOpcode::LOAD_INT16;  break;
                case ColumnType::INT32:  op = SamplerOpcode::LOAD_INT32;  break;
                case ColumnType::INT64:  op = SamplerOpcode::LOAD_INT64;  break;
                case ColumnType::UINT8:  op = SamplerOpcode::LOAD_UINT8;  break;
                case ColumnType::UINT16: op = SamplerOpcode::LOAD_UINT16; break;
                case ColumnType::UINT32: op = SamplerOpcode::LOAD_UINT32; break;
                case ColumnType::UINT64: op = SamplerOpcode::LOAD_UINT64; break;
                case ColumnType::FLOAT:  op = SamplerOpcode::LOAD_FLOAT;  break;
                case ColumnType::DOUBLE: op = SamplerOpcode::LOAD_DOUBLE; break;
                case ColumnType::STRING: op = SamplerOpcode::LOAD_STRING; break;
                default:                 op = SamplerOpcode::LOAD_INT64;  break;
            }
            emit(op);
            emitI16(row_off);
            emitU16(col);
        }

        // ── AST compilation ─────────────────────────────────────────

        void compileNode(const AstNode& node) {
            std::visit([&](const auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                if      constexpr (std::is_same_v<T, CellRef>)     compileCellRef(node, payload);
                else if constexpr (std::is_same_v<T, LiteralNode>) compileLiteral(node, payload);
                else if constexpr (std::is_same_v<T, BinaryNode>)  compileBinary(node, payload);
                else if constexpr (std::is_same_v<T, UnaryNode>)   compileUnary(node, payload);
                else if constexpr (std::is_same_v<T, RowRef>)      {} // handled by selection
            }, node.kind);
        }

        void compileCellRef(const AstNode& node, const CellRef& cr) {
            ColumnType ct = layout_.columnType(cr.col_index);
            emitLoad(cr.row_offset, cr.col_index, ct);
        }

        void compileLiteral(const AstNode& /*node*/, const LiteralNode& ln) {
            std::visit([&](const auto& val) {
                using V = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<V, bool>) {
                    emit(SamplerOpcode::CONST_BOOL);
                    emitU8(val ? 1 : 0);
                } else if constexpr (std::is_same_v<V, int64_t>) {
                    emit(SamplerOpcode::CONST_INT);
                    emitI64(val);
                } else if constexpr (std::is_same_v<V, uint64_t>) {
                    // Store as int64 (per promotion rules)
                    emit(SamplerOpcode::CONST_INT);
                    emitI64(static_cast<int64_t>(val));
                } else if constexpr (std::is_same_v<V, double>) {
                    emit(SamplerOpcode::CONST_FLOAT);
                    emitF64(val);
                } else if constexpr (std::is_same_v<V, std::string>) {
                    uint16_t idx = static_cast<uint16_t>(bc_.string_pool.size());
                    bc_.string_pool.push_back(val);
                    emit(SamplerOpcode::CONST_STRING);
                    emitU16(idx);
                }
            }, ln.value);
        }

        void compileBinary(const AstNode& node, const BinaryNode& bn) {
            ExprType lt = bn.left->resolved_type;
            ExprType rt = bn.right->resolved_type;

            // Short-circuit for && and ||
            if (bn.op == BinaryOp::AND) {
                compileNode(*bn.left);
                ensureBool(*bn.left);
                emit(SamplerOpcode::JUMP_IF_FALSE);
                size_t patch_pos = codePos();
                emitI16(0); // placeholder
                emit(SamplerOpcode::POP); // discard A (it was true)
                compileNode(*bn.right);
                ensureBool(*bn.right);
                int16_t offset = static_cast<int16_t>(codePos() - (patch_pos + 2));
                patchI16(patch_pos, offset);
                return;
            }

            if (bn.op == BinaryOp::OR) {
                compileNode(*bn.left);
                ensureBool(*bn.left);
                emit(SamplerOpcode::JUMP_IF_TRUE);
                size_t patch_pos = codePos();
                emitI16(0); // placeholder
                emit(SamplerOpcode::POP); // discard A (it was false)
                compileNode(*bn.right);
                ensureBool(*bn.right);
                int16_t offset = static_cast<int16_t>(codePos() - (patch_pos + 2));
                patchI16(patch_pos, offset);
                return;
            }

            // Determine the effective type for the operation
            ExprType eff;
            if (isComparisonOp(bn.op)) {
                eff = promoteForComparison(lt, rt);
            } else {
                eff = node.resolved_type;
            }

            // Compile left, then promote left while it's TOS
            compileNode(*bn.left);
            if (isArithmeticOp(bn.op) || isComparisonOp(bn.op) || isBitwiseOp(bn.op)) {
                emitPromotion(lt, eff);
            }

            // Compile right, then promote right while it's TOS
            compileNode(*bn.right);
            if (isArithmeticOp(bn.op) || isComparisonOp(bn.op) || isBitwiseOp(bn.op)) {
                emitPromotion(rt, eff);
            }

            emitBinaryOp(bn.op, eff);
        }

        void compileUnary(const AstNode& /*node*/, const UnaryNode& un) {
            compileNode(*un.operand);
            ExprType ot = un.operand->resolved_type;

            switch (un.op) {
                case UnaryOp::NEG:
                    if (ot == ExprType::UINT) {
                        emit(SamplerOpcode::PROMOTE_UINT_TO_INT);
                        emit(SamplerOpcode::NEG_INT);
                    } else if (ot == ExprType::INT) {
                        emit(SamplerOpcode::NEG_INT);
                    } else {
                        emit(SamplerOpcode::NEG_FLOAT);
                    }
                    break;

                case UnaryOp::NOT:
                    ensureBool(*un.operand);
                    emit(SamplerOpcode::BOOL_NOT);
                    break;

                case UnaryOp::BIT_NOT:
                    if (ot == ExprType::UINT)
                        emit(SamplerOpcode::PROMOTE_UINT_TO_INT);
                    else if (ot == ExprType::BOOL)
                        emit(SamplerOpcode::PROMOTE_BOOL_TO_INT);
                    emit(SamplerOpcode::BIT_NOT);
                    break;
            }
        }

        // ── Ensure bool on top of stack ─────────────────────────────

        void ensureBool(const AstNode& node) {
            ExprType t = node.resolved_type;
            if (t == ExprType::BOOL) return;
            if (t == ExprType::INT)   { emit(SamplerOpcode::INT_TO_BOOL);   return; }
            if (t == ExprType::UINT)  { emit(SamplerOpcode::UINT_TO_BOOL);  return; }
            if (t == ExprType::FLOAT) { emit(SamplerOpcode::FLOAT_TO_BOOL); return; }
        }

        // ── Promotion helpers ───────────────────────────────────────

        void emitPromotion(ExprType from, ExprType to) {
            if (from == to) return;
            if (from == ExprType::INT  && to == ExprType::FLOAT) emit(SamplerOpcode::PROMOTE_INT_TO_FLOAT);
            if (from == ExprType::UINT && to == ExprType::INT)   emit(SamplerOpcode::PROMOTE_UINT_TO_INT);
            if (from == ExprType::UINT && to == ExprType::FLOAT) emit(SamplerOpcode::PROMOTE_UINT_TO_FLOAT);
            if (from == ExprType::BOOL && to == ExprType::INT)   emit(SamplerOpcode::PROMOTE_BOOL_TO_INT);
            if (from == ExprType::BOOL && to == ExprType::FLOAT) {
                emit(SamplerOpcode::PROMOTE_BOOL_TO_INT);
                emit(SamplerOpcode::PROMOTE_INT_TO_FLOAT);
            }
        }

        // ── Binary op emission ──────────────────────────────────────

        void emitBinaryOp(BinaryOp op, ExprType eff) {
            switch (op) {
                case BinaryOp::ADD:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::ADD_FLOAT : SamplerOpcode::ADD_INT);
                    break;
                case BinaryOp::SUB:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::SUB_FLOAT : SamplerOpcode::SUB_INT);
                    break;
                case BinaryOp::MUL:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::MUL_FLOAT : SamplerOpcode::MUL_INT);
                    break;
                case BinaryOp::DIV:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::DIV_FLOAT : SamplerOpcode::DIV_INT);
                    break;
                case BinaryOp::MOD:
                    emit(SamplerOpcode::MOD_INT);
                    break;
                case BinaryOp::EQ:
                    if (eff == ExprType::STRING)     emit(SamplerOpcode::CMP_EQ_STRING);
                    else if (eff == ExprType::FLOAT) emit(SamplerOpcode::CMP_EQ_FLOAT);
                    else                             emit(SamplerOpcode::CMP_EQ_INT);
                    break;
                case BinaryOp::NE:
                    if (eff == ExprType::STRING)     emit(SamplerOpcode::CMP_NE_STRING);
                    else if (eff == ExprType::FLOAT) emit(SamplerOpcode::CMP_NE_FLOAT);
                    else                             emit(SamplerOpcode::CMP_NE_INT);
                    break;
                case BinaryOp::LT:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::CMP_LT_FLOAT : SamplerOpcode::CMP_LT_INT);
                    break;
                case BinaryOp::LE:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::CMP_LE_FLOAT : SamplerOpcode::CMP_LE_INT);
                    break;
                case BinaryOp::GT:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::CMP_GT_FLOAT : SamplerOpcode::CMP_GT_INT);
                    break;
                case BinaryOp::GE:
                    emit(eff == ExprType::FLOAT ? SamplerOpcode::CMP_GE_FLOAT : SamplerOpcode::CMP_GE_INT);
                    break;
                case BinaryOp::BIT_AND: emit(SamplerOpcode::BIT_AND); break;
                case BinaryOp::BIT_OR:  emit(SamplerOpcode::BIT_OR);  break;
                case BinaryOp::BIT_XOR: emit(SamplerOpcode::BIT_XOR); break;
                case BinaryOp::SHL:     emit(SamplerOpcode::BIT_SHL); break;
                case BinaryOp::SHR:     emit(SamplerOpcode::BIT_SHR); break;
                case BinaryOp::AND:     emit(SamplerOpcode::BOOL_AND); break;
                case BinaryOp::OR:      emit(SamplerOpcode::BOOL_OR);  break;
            }
        }

        static bool isComparisonOp(BinaryOp op) {
            return op >= BinaryOp::EQ && op <= BinaryOp::GE;
        }

        static bool isArithmeticOp(BinaryOp op) {
            return op >= BinaryOp::ADD && op <= BinaryOp::MOD;
        }

        static bool isBitwiseOp(BinaryOp op) {
            return op >= BinaryOp::BIT_AND && op <= BinaryOp::SHR;
        }

        static ExprType promoteForComparison(ExprType a, ExprType b) {
            if (a == ExprType::STRING && b == ExprType::STRING) return ExprType::STRING;
            if (a == ExprType::FLOAT || b == ExprType::FLOAT) return ExprType::FLOAT;
            if (a == ExprType::INT || b == ExprType::INT) return ExprType::INT;
            if (a == ExprType::UINT || b == ExprType::UINT) return ExprType::UINT;
            return ExprType::INT; // bool promoted to int for comparison
        }

        // ── Opcode name for disassembly ─────────────────────────────

        static const char* opcodeName(SamplerOpcode op) {
            switch (op) {
                case SamplerOpcode::LOAD_BOOL:     return "LOAD_BOOL";
                case SamplerOpcode::LOAD_INT8:     return "LOAD_INT8";
                case SamplerOpcode::LOAD_INT16:    return "LOAD_INT16";
                case SamplerOpcode::LOAD_INT32:    return "LOAD_INT32";
                case SamplerOpcode::LOAD_INT64:    return "LOAD_INT64";
                case SamplerOpcode::LOAD_UINT8:    return "LOAD_UINT8";
                case SamplerOpcode::LOAD_UINT16:   return "LOAD_UINT16";
                case SamplerOpcode::LOAD_UINT32:   return "LOAD_UINT32";
                case SamplerOpcode::LOAD_UINT64:   return "LOAD_UINT64";
                case SamplerOpcode::LOAD_FLOAT:    return "LOAD_FLOAT";
                case SamplerOpcode::LOAD_DOUBLE:   return "LOAD_DOUBLE";
                case SamplerOpcode::LOAD_STRING:   return "LOAD_STRING";
                case SamplerOpcode::CONST_BOOL:    return "CONST_BOOL";
                case SamplerOpcode::CONST_INT:     return "CONST_INT";
                case SamplerOpcode::CONST_UINT:    return "CONST_UINT";
                case SamplerOpcode::CONST_FLOAT:   return "CONST_FLOAT";
                case SamplerOpcode::CONST_STRING:  return "CONST_STRING";
                case SamplerOpcode::ADD_INT:        return "ADD_INT";
                case SamplerOpcode::ADD_FLOAT:      return "ADD_FLOAT";
                case SamplerOpcode::SUB_INT:        return "SUB_INT";
                case SamplerOpcode::SUB_FLOAT:      return "SUB_FLOAT";
                case SamplerOpcode::MUL_INT:        return "MUL_INT";
                case SamplerOpcode::MUL_FLOAT:      return "MUL_FLOAT";
                case SamplerOpcode::DIV_INT:        return "DIV_INT";
                case SamplerOpcode::DIV_FLOAT:      return "DIV_FLOAT";
                case SamplerOpcode::MOD_INT:        return "MOD_INT";
                case SamplerOpcode::NEG_INT:        return "NEG_INT";
                case SamplerOpcode::NEG_FLOAT:      return "NEG_FLOAT";
                case SamplerOpcode::BIT_AND:        return "BIT_AND";
                case SamplerOpcode::BIT_OR:         return "BIT_OR";
                case SamplerOpcode::BIT_XOR:        return "BIT_XOR";
                case SamplerOpcode::BIT_NOT:        return "BIT_NOT";
                case SamplerOpcode::BIT_SHL:        return "BIT_SHL";
                case SamplerOpcode::BIT_SHR:        return "BIT_SHR";
                case SamplerOpcode::PROMOTE_INT_TO_FLOAT:   return "PROMOTE_INT_TO_FLOAT";
                case SamplerOpcode::PROMOTE_UINT_TO_INT:    return "PROMOTE_UINT_TO_INT";
                case SamplerOpcode::PROMOTE_UINT_TO_FLOAT:  return "PROMOTE_UINT_TO_FLOAT";
                case SamplerOpcode::PROMOTE_BOOL_TO_INT:    return "PROMOTE_BOOL_TO_INT";
                case SamplerOpcode::CMP_EQ_INT:    return "CMP_EQ_INT";
                case SamplerOpcode::CMP_EQ_FLOAT:  return "CMP_EQ_FLOAT";
                case SamplerOpcode::CMP_EQ_STRING: return "CMP_EQ_STRING";
                case SamplerOpcode::CMP_NE_INT:    return "CMP_NE_INT";
                case SamplerOpcode::CMP_NE_FLOAT:  return "CMP_NE_FLOAT";
                case SamplerOpcode::CMP_NE_STRING: return "CMP_NE_STRING";
                case SamplerOpcode::CMP_LT_INT:    return "CMP_LT_INT";
                case SamplerOpcode::CMP_LT_FLOAT:  return "CMP_LT_FLOAT";
                case SamplerOpcode::CMP_LE_INT:    return "CMP_LE_INT";
                case SamplerOpcode::CMP_LE_FLOAT:  return "CMP_LE_FLOAT";
                case SamplerOpcode::CMP_GT_INT:    return "CMP_GT_INT";
                case SamplerOpcode::CMP_GT_FLOAT:  return "CMP_GT_FLOAT";
                case SamplerOpcode::CMP_GE_INT:    return "CMP_GE_INT";
                case SamplerOpcode::CMP_GE_FLOAT:  return "CMP_GE_FLOAT";
                case SamplerOpcode::POP:           return "POP";
                case SamplerOpcode::BOOL_NOT:      return "BOOL_NOT";
                case SamplerOpcode::BOOL_AND:      return "BOOL_AND";
                case SamplerOpcode::BOOL_OR:       return "BOOL_OR";
                case SamplerOpcode::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
                case SamplerOpcode::JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
                case SamplerOpcode::INT_TO_BOOL:   return "INT_TO_BOOL";
                case SamplerOpcode::UINT_TO_BOOL:  return "UINT_TO_BOOL";
                case SamplerOpcode::FLOAT_TO_BOOL: return "FLOAT_TO_BOOL";
                case SamplerOpcode::HALT_COND:     return "HALT_COND";
                case SamplerOpcode::EMIT:          return "EMIT";
                case SamplerOpcode::HALT_SEL:      return "HALT_SEL";
            }
            return "UNKNOWN";
        }
    };

} // namespace bcsv
