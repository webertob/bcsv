/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace bcsv {

    // ── AST Node Types ──────────────────────────────────────────────
    //
    // The expression AST is produced by the Parser and consumed by the
    // TypeResolver and BytecodeCompiler.  Nodes are heap-allocated via
    // unique_ptr; the tree is owned by the Sampler and discarded after
    // bytecode compilation.

    /// Resolved expression type (set by TypeResolver, initially UNRESOLVED)
    enum class ExprType : uint8_t {
        UNRESOLVED,
        BOOL,
        INT,        // int64_t
        UINT,       // uint64_t
        FLOAT,      // double
        STRING,
    };

    inline const char* toString(ExprType t) {
        switch (t) {
            case ExprType::UNRESOLVED: return "unresolved";
            case ExprType::BOOL:       return "Bool";
            case ExprType::INT:        return "Int";
            case ExprType::UINT:       return "UInt";
            case ExprType::FLOAT:      return "Float";
            case ExprType::STRING:     return "String";
        }
        return "unknown";
    }

    // Forward declarations
    struct AstNode;
    using AstNodePtr = std::unique_ptr<AstNode>;

    // ── Literal value ───────────────────────────────────────────────
    using LiteralValue = std::variant<bool, int64_t, uint64_t, double, std::string>;

    // ── Operator enums ──────────────────────────────────────────────

    enum class BinaryOp : uint8_t {
        // Arithmetic
        ADD, SUB, MUL, DIV, MOD,
        // Comparison
        EQ, NE, LT, LE, GT, GE,
        // Boolean
        AND, OR,
        // Bitwise
        BIT_AND, BIT_OR, BIT_XOR, SHL, SHR,
    };

    enum class UnaryOp : uint8_t {
        NEG,        // -x  (arithmetic negation)
        NOT,        // !x  (logical negation)
        BIT_NOT,    // ~x  (bitwise NOT)
    };

    // ── AST Node ────────────────────────────────────────────────────
    //
    // Variant-based node.  Each node carries:
    //   - source_pos:  character offset in the original expression string
    //   - resolved_type: filled in by TypeResolver (initially UNRESOLVED)
    //   - kind:  one of the payload structs below

    /// Cell reference: X[row_offset][col_spec]
    struct CellRef {
        int16_t  row_offset;            // e.g. -1, 0, +3
        bool     is_wildcard = false;   // true if col_spec is '*'
        bool     is_name     = false;   // true if col_spec is a string (name)
        uint16_t col_index   = 0;       // resolved numeric index (set by TypeResolver)
        std::string col_name;           // original name (when is_name == true)
    };

    /// Literal constant
    struct LiteralNode {
        LiteralValue value;
    };

    /// Binary operation
    struct BinaryNode {
        BinaryOp    op;
        AstNodePtr  left;
        AstNodePtr  right;
    };

    /// Unary operation
    struct UnaryNode {
        UnaryOp    op;
        AstNodePtr operand;
    };

    /// A single parsed cell reference (for whole-row X[r] without col)
    struct RowRef {
        int16_t row_offset;
    };

    /// Top-level variant node
    struct AstNode {
        size_t   source_pos    = 0;
        ExprType resolved_type = ExprType::UNRESOLVED;

        using Kind = std::variant<CellRef, LiteralNode, BinaryNode, UnaryNode, RowRef>;
        Kind kind;

        // Convenience constructors
        explicit AstNode(CellRef cr,      size_t pos = 0) : source_pos(pos), kind(std::move(cr)) {}
        explicit AstNode(LiteralNode ln,  size_t pos = 0) : source_pos(pos), kind(std::move(ln)) {}
        explicit AstNode(BinaryNode bn,   size_t pos = 0) : source_pos(pos), kind(std::move(bn)) {}
        explicit AstNode(UnaryNode un,    size_t pos = 0) : source_pos(pos), kind(std::move(un)) {}
        explicit AstNode(RowRef rr,       size_t pos = 0) : source_pos(pos), kind(std::move(rr)) {}

        // Type-check helpers
        bool is_cell_ref()  const { return std::holds_alternative<CellRef>(kind); }
        bool is_literal()   const { return std::holds_alternative<LiteralNode>(kind); }
        bool is_binary()    const { return std::holds_alternative<BinaryNode>(kind); }
        bool is_unary()     const { return std::holds_alternative<UnaryNode>(kind); }
        bool is_row_ref()   const { return std::holds_alternative<RowRef>(kind); }

        CellRef&       as_cell_ref()  { return std::get<CellRef>(kind); }
        LiteralNode&   as_literal()   { return std::get<LiteralNode>(kind); }
        BinaryNode&    as_binary()    { return std::get<BinaryNode>(kind); }
        UnaryNode&     as_unary()     { return std::get<UnaryNode>(kind); }
        RowRef&        as_row_ref()   { return std::get<RowRef>(kind); }

        const CellRef&       as_cell_ref()  const { return std::get<CellRef>(kind); }
        const LiteralNode&   as_literal()   const { return std::get<LiteralNode>(kind); }
        const BinaryNode&    as_binary()    const { return std::get<BinaryNode>(kind); }
        const UnaryNode&     as_unary()     const { return std::get<UnaryNode>(kind); }
        const RowRef&        as_row_ref()   const { return std::get<RowRef>(kind); }
    };

    /// Selection expression: a list of items (each is an arithmetic expression or wildcard)
    struct SelectionExpr {
        std::vector<AstNodePtr> items;
    };

} // namespace bcsv
