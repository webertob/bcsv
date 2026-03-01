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
#include "definitions.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>

namespace bcsv {

    // ── Type Resolver ───────────────────────────────────────────────
    //
    // Walks the AST and:
    //   1. Resolves cell references against the source Layout
    //      (column name → index, validates index range)
    //   2. Infers and sets resolved_type on every node
    //   3. Collects min/max row offsets for window sizing
    //   4. Reports type errors (e.g. string + int)
    //
    // This phase runs at compile time (setConditional/setSelection).
    // After it succeeds, every node has a concrete ExprType and
    // every CellRef has a resolved col_index.

    struct TypeResolveResult {
        bool        success = false;
        std::string error_msg;
        size_t      error_position = 0;
        int16_t     min_offset = 0;     // most negative row offset
        int16_t     max_offset = 0;     // most positive row offset
    };

    /// Map a BCSV ColumnType to an expression ExprType.
    inline ExprType columnTypeToExprType(ColumnType ct) {
        switch (ct) {
            case ColumnType::BOOL:   return ExprType::BOOL;
            case ColumnType::INT8:
            case ColumnType::INT16:
            case ColumnType::INT32:
            case ColumnType::INT64:  return ExprType::INT;
            case ColumnType::UINT8:
            case ColumnType::UINT16:
            case ColumnType::UINT32:
            case ColumnType::UINT64: return ExprType::UINT;
            case ColumnType::FLOAT:
            case ColumnType::DOUBLE: return ExprType::FLOAT;
            case ColumnType::STRING: return ExprType::STRING;
            default:                 return ExprType::UNRESOLVED;
        }
    }

    template<typename LayoutType>
    class TypeResolver {
    public:
        explicit TypeResolver(const LayoutType& layout)
            : layout_(layout) {}

        TypeResolveResult resolve(AstNode& root) {
            result_ = {};
            resolveNode(root);
            result_.success = result_.error_msg.empty();
            return result_;
        }

        TypeResolveResult resolveSelection(SelectionExpr& sel) {
            result_ = {};
            for (auto& item : sel.items) {
                if (!item) continue;
                // RowRef special handling: expand to wildcard
                if (item->is_row_ref()) {
                    // Convert RowRef to a wildcard CellRef for consistency
                    auto& rr = item->as_row_ref();
                    trackOffset(rr.row_offset);
                    // Mark as resolved — wildcard expansion happens in compiler
                    item->resolved_type = ExprType::UNRESOLVED; // special: wildcard
                } else {
                    resolveNode(*item);
                }
            }
            result_.success = result_.error_msg.empty();
            return result_;
        }

    private:
        const LayoutType& layout_;
        TypeResolveResult result_;

        void trackOffset(int16_t offset) {
            result_.min_offset = std::min(result_.min_offset, offset);
            result_.max_offset = std::max(result_.max_offset, offset);
        }

        void error(const AstNode& node, const std::string& msg) {
            if (!result_.error_msg.empty()) return; // first error wins
            result_.error_msg = msg;
            result_.error_position = node.source_pos;
        }

        void resolveNode(AstNode& node) {
            if (!result_.error_msg.empty()) return;

            std::visit([&](auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                if      constexpr (std::is_same_v<T, CellRef>)     resolveCellRef(node, payload);
                else if constexpr (std::is_same_v<T, LiteralNode>) resolveLiteral(node, payload);
                else if constexpr (std::is_same_v<T, BinaryNode>)  resolveBinary(node, payload);
                else if constexpr (std::is_same_v<T, UnaryNode>)   resolveUnary(node, payload);
                else if constexpr (std::is_same_v<T, RowRef>)      resolveRowRef(node, payload);
            }, node.kind);
        }

        // ── Cell reference resolution ───────────────────────────────

        void resolveCellRef(AstNode& node, CellRef& cr) {
            trackOffset(cr.row_offset);

            if (cr.is_wildcard) {
                // Wildcard — type is "multi", handled specially by compiler
                node.resolved_type = ExprType::UNRESOLVED;
                return;
            }

            // Resolve column name → index
            if (cr.is_name) {
                if (!layout_.hasColumn(cr.col_name)) {
                    error(node, "unknown column name: \"" + cr.col_name + "\"");
                    return;
                }
                cr.col_index = static_cast<uint16_t>(layout_.columnIndex(cr.col_name));
            }

            // Validate column index
            if (cr.col_index >= layout_.columnCount()) {
                error(node, "column index " + std::to_string(cr.col_index) +
                      " out of range (layout has " +
                      std::to_string(layout_.columnCount()) + " columns)");
                return;
            }

            // Set type from layout
            ColumnType ct = layout_.columnType(cr.col_index);
            node.resolved_type = columnTypeToExprType(ct);
        }

        // ── Row reference ───────────────────────────────────────────

        void resolveRowRef(AstNode& node, RowRef& rr) {
            trackOffset(rr.row_offset);
            node.resolved_type = ExprType::UNRESOLVED; // wildcard expansion
        }

        // ── Literal resolution ──────────────────────────────────────

        void resolveLiteral(AstNode& node, LiteralNode& ln) {
            std::visit([&](const auto& val) {
                using V = std::decay_t<decltype(val)>;
                if      constexpr (std::is_same_v<V, bool>)        node.resolved_type = ExprType::BOOL;
                else if constexpr (std::is_same_v<V, int64_t>)     node.resolved_type = ExprType::INT;
                else if constexpr (std::is_same_v<V, uint64_t>)    node.resolved_type = ExprType::UINT;
                else if constexpr (std::is_same_v<V, double>)      node.resolved_type = ExprType::FLOAT;
                else if constexpr (std::is_same_v<V, std::string>) node.resolved_type = ExprType::STRING;
            }, ln.value);
        }

        // ── Binary operation ────────────────────────────────────────

        void resolveBinary(AstNode& node, BinaryNode& bn) {
            resolveNode(*bn.left);
            resolveNode(*bn.right);
            if (!result_.error_msg.empty()) return;

            ExprType lt = bn.left->resolved_type;
            ExprType rt = bn.right->resolved_type;

            switch (bn.op) {
                // ── Comparison operators ─────────────────────────────
                case BinaryOp::EQ:
                case BinaryOp::NE:
                    if (lt == ExprType::STRING || rt == ExprType::STRING) {
                        if (lt != rt) {
                            error(node, "cannot compare String with " +
                                  std::string(toString(lt == ExprType::STRING ? rt : lt)));
                            return;
                        }
                    }
                    node.resolved_type = ExprType::BOOL;
                    return;

                case BinaryOp::LT:
                case BinaryOp::LE:
                case BinaryOp::GT:
                case BinaryOp::GE:
                    if (lt == ExprType::STRING || rt == ExprType::STRING) {
                        error(node, "comparison operators <, <=, >, >= not supported for String operands");
                        return;
                    }
                    node.resolved_type = ExprType::BOOL;
                    return;

                // ── Boolean operators ────────────────────────────────
                case BinaryOp::AND:
                case BinaryOp::OR:
                    // Both sides must be bool-convertible
                    if (lt == ExprType::STRING || rt == ExprType::STRING) {
                        error(node, "cannot use String in boolean context");
                        return;
                    }
                    node.resolved_type = ExprType::BOOL;
                    return;

                // ── Bitwise operators ────────────────────────────────
                case BinaryOp::BIT_AND:
                case BinaryOp::BIT_OR:
                case BinaryOp::BIT_XOR:
                case BinaryOp::SHL:
                case BinaryOp::SHR:
                    if (lt == ExprType::STRING || rt == ExprType::STRING ||
                        lt == ExprType::FLOAT  || rt == ExprType::FLOAT) {
                        error(node, "bitwise operators require integer operands");
                        return;
                    }
                    node.resolved_type = ExprType::INT; // bitwise always produces int
                    return;

                // ── Arithmetic operators ─────────────────────────────
                case BinaryOp::ADD:
                case BinaryOp::SUB:
                case BinaryOp::MUL:
                case BinaryOp::DIV:
                    if (lt == ExprType::STRING || rt == ExprType::STRING) {
                        error(node, "cannot apply arithmetic to String");
                        return;
                    }
                    node.resolved_type = promoteArithmetic(lt, rt);
                    return;

                case BinaryOp::MOD:
                    if (lt == ExprType::STRING || rt == ExprType::STRING ||
                        lt == ExprType::FLOAT  || rt == ExprType::FLOAT) {
                        error(node, "modulo operator requires integer operands");
                        return;
                    }
                    node.resolved_type = ExprType::INT;
                    return;
            }
        }

        // ── Unary operation ─────────────────────────────────────────

        void resolveUnary(AstNode& node, UnaryNode& un) {
            resolveNode(*un.operand);
            if (!result_.error_msg.empty()) return;

            ExprType ot = un.operand->resolved_type;

            switch (un.op) {
                case UnaryOp::NEG:
                    if (ot == ExprType::STRING || ot == ExprType::BOOL) {
                        error(node, "cannot negate " + std::string(toString(ot)));
                        return;
                    }
                    node.resolved_type = (ot == ExprType::UINT) ? ExprType::INT : ot;
                    return;

                case UnaryOp::NOT:
                    if (ot == ExprType::STRING) {
                        error(node, "cannot apply ! to String");
                        return;
                    }
                    node.resolved_type = ExprType::BOOL;
                    return;

                case UnaryOp::BIT_NOT:
                    if (ot == ExprType::STRING || ot == ExprType::FLOAT) {
                        error(node, "bitwise NOT requires integer operand");
                        return;
                    }
                    node.resolved_type = ExprType::INT;
                    return;
            }
        }

        // ── Type promotion ──────────────────────────────────────────
        //
        //  Rules (matching §5.3 of the plan):
        //    INT  op FLOAT → FLOAT
        //    UINT op INT   → INT
        //    UINT op FLOAT → FLOAT
        //    BOOL in arith → INT (0/1)

        static ExprType promoteArithmetic(ExprType a, ExprType b) {
            // If either is FLOAT, result is FLOAT
            if (a == ExprType::FLOAT || b == ExprType::FLOAT)
                return ExprType::FLOAT;
            // UINT op INT → INT
            if ((a == ExprType::UINT && b == ExprType::INT) ||
                (a == ExprType::INT  && b == ExprType::UINT))
                return ExprType::INT;
            // Both INT or both UINT
            if (a == ExprType::INT || b == ExprType::INT)
                return ExprType::INT;
            if (a == ExprType::UINT || b == ExprType::UINT)
                return ExprType::UINT;
            // BOOL arithmetic → INT
            return ExprType::INT;
        }
    };

} // namespace bcsv
