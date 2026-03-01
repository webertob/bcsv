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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <limits>
#include <utility>
#include <vector>

namespace bcsv {

    // ── Sampler Parse Result ────────────────────────────────────────

    struct SamplerParseResult {
        bool        success = false;
        std::string error_msg;
        size_t      error_position = 0;
    };

    /// Result of parsing a selection expression (move-only due to unique_ptr)
    struct SelectionParseResult {
        SelectionExpr       selection;
        SamplerParseResult  parse_result;

        SelectionParseResult() = default;
        SelectionParseResult(SelectionExpr s, SamplerParseResult r)
            : selection(std::move(s)), parse_result(std::move(r)) {}

        // Move-only
        SelectionParseResult(SelectionParseResult&&) noexcept = default;
        SelectionParseResult& operator=(SelectionParseResult&&) noexcept = default;
        SelectionParseResult(const SelectionParseResult&) = delete;
        SelectionParseResult& operator=(const SelectionParseResult&) = delete;
    };

    // ── Pratt Parser ────────────────────────────────────────────────
    //
    // Parses conditional and selection expressions into an AST.
    // Uses Pratt (top-down operator precedence) parsing for compact
    // handling of 12 precedence levels.

    class SamplerParser {
    public:
        // ── Public API ──────────────────────────────────────────────

        /// Parse a conditional expression → single AST node (bool_expr)
        static std::pair<AstNodePtr, SamplerParseResult>
        parseConditional(std::string_view source) {
            auto tokens = Tokenizer::tokenize(source);
            SamplerParser parser(tokens);

            if (parser.check(TokenType::ERROR)) {
                return {nullptr, parser.errorResult()};
            }

            auto node = parser.parseExpression(0);
            if (!node) return {nullptr, parser.result_};

            if (!parser.check(TokenType::END_OF_INPUT)) {
                parser.error("unexpected token after expression");
                return {nullptr, parser.result_};
            }

            SamplerParseResult ok;
            ok.success = true;
            return {std::move(node), ok};
        }

        /// Parse a selection expression → list of expression items
        static SelectionParseResult
        parseSelection(std::string_view source) {
            auto tokens = Tokenizer::tokenize(source);
            SamplerParser parser(tokens);

            if (parser.check(TokenType::ERROR)) {
                return SelectionParseResult{{}, parser.errorResult()};
            }

            SelectionExpr sel;

            // First item
            auto item = parser.parseExpression(0);
            if (!item) return SelectionParseResult{{}, std::move(parser.result_)};
            sel.items.push_back(std::move(item));

            // Remaining items (comma-separated)
            while (parser.match(TokenType::COMMA)) {
                item = parser.parseExpression(0);
                if (!item) return SelectionParseResult{{}, std::move(parser.result_)};
                sel.items.push_back(std::move(item));
            }

            if (!parser.check(TokenType::END_OF_INPUT)) {
                parser.error("unexpected token after selection expression");
                return SelectionParseResult{{}, std::move(parser.result_)};
            }

            SamplerParseResult ok;
            ok.success = true;
            return SelectionParseResult{std::move(sel), ok};
        }

    private:
        const std::vector<Token>& tokens_;
        size_t                    current_ = 0;
        SamplerParseResult        result_;

        explicit SamplerParser(const std::vector<Token>& tokens) : tokens_(tokens) {}

        // ── Precedence levels (matching C, low → high) ──────────────
        // 
        //  0: ||
        //  1: &&
        //  2: |   (bitwise OR)
        //  3: ^   (bitwise XOR)
        //  4: &   (bitwise AND)
        //  5: ==  !=
        //  6: <   <=  >   >=
        //  7: <<  >>
        //  8: +   -
        //  9: *   /   %
        // 10: unary (-, !, ~) — handled in prefix
        // 11: atoms (literals, cell_ref, grouped)

        static int prefixBindingPower(TokenType type) {
            switch (type) {
                case TokenType::MINUS:
                case TokenType::BANG:
                case TokenType::TILDE:
                    return 21;  // right-binding, high precedence
                default:
                    return -1;
            }
        }

        struct InfixBP {
            int left;
            int right;
        };

        static InfixBP infixBindingPower(TokenType type) {
            switch (type) {
                case TokenType::OR:      return {1, 2};    // ||
                case TokenType::AND:     return {3, 4};    // &&
                case TokenType::PIPE:    return {5, 6};    // |
                case TokenType::CARET:   return {7, 8};    // ^
                case TokenType::AMP:     return {9, 10};   // &
                case TokenType::EQ:
                case TokenType::NE:      return {11, 12};  // == !=
                case TokenType::LT:
                case TokenType::LE:
                case TokenType::GT:
                case TokenType::GE:      return {13, 14};  // < <= > >=
                case TokenType::SHL:
                case TokenType::SHR:     return {15, 16};  // << >>
                case TokenType::PLUS:
                case TokenType::MINUS:   return {17, 18};  // + -
                case TokenType::STAR:
                case TokenType::SLASH:
                case TokenType::PERCENT: return {19, 20};  // * / %
                default:                 return {-1, -1};
            }
        }

        static BinaryOp toBinaryOp(TokenType type) {
            switch (type) {
                case TokenType::PLUS:    return BinaryOp::ADD;
                case TokenType::MINUS:   return BinaryOp::SUB;
                case TokenType::STAR:    return BinaryOp::MUL;
                case TokenType::SLASH:   return BinaryOp::DIV;
                case TokenType::PERCENT: return BinaryOp::MOD;
                case TokenType::EQ:      return BinaryOp::EQ;
                case TokenType::NE:      return BinaryOp::NE;
                case TokenType::LT:      return BinaryOp::LT;
                case TokenType::LE:      return BinaryOp::LE;
                case TokenType::GT:      return BinaryOp::GT;
                case TokenType::GE:      return BinaryOp::GE;
                case TokenType::AND:     return BinaryOp::AND;
                case TokenType::OR:      return BinaryOp::OR;
                case TokenType::AMP:     return BinaryOp::BIT_AND;
                case TokenType::PIPE:    return BinaryOp::BIT_OR;
                case TokenType::CARET:   return BinaryOp::BIT_XOR;
                case TokenType::SHL:     return BinaryOp::SHL;
                case TokenType::SHR:     return BinaryOp::SHR;
                default:                 return BinaryOp::ADD; // unreachable
            }
        }

        static UnaryOp toUnaryOp(TokenType type) {
            switch (type) {
                case TokenType::MINUS: return UnaryOp::NEG;
                case TokenType::BANG:  return UnaryOp::NOT;
                case TokenType::TILDE: return UnaryOp::BIT_NOT;
                default:               return UnaryOp::NEG; // unreachable
            }
        }

        // ── Token navigation ────────────────────────────────────────

        const Token& current() const { return tokens_[current_]; }

        bool check(TokenType type) const { return current().type == type; }

        bool match(TokenType type) {
            if (check(type)) { current_++; return true; }
            return false;
        }

        Token consume(TokenType type, const char* msg) {
            if (check(type)) {
                Token tok = tokens_[current_];
                current_++;
                return tok;
            }
            error(msg);
            return {};
        }

        void error(const std::string& msg) {
            if (result_.success || result_.error_msg.empty()) {
                result_.success = false;
                result_.error_msg = msg;
                result_.error_position = current().pos;
            }
        }

        bool hasError() const { return !result_.error_msg.empty(); }

        SamplerParseResult errorResult() const {
            if (check(TokenType::ERROR)) {
                SamplerParseResult r;
                r.success = false;
                r.error_msg = current().text;
                r.error_position = current().pos;
                return r;
            }
            return result_;
        }

        // ── Pratt parser core ───────────────────────────────────────

        AstNodePtr parseExpression(int minBP) {
            if (hasError()) return nullptr;

            // Prefix
            AstNodePtr left = parsePrefix();
            if (!left) return nullptr;

            // Infix loop
            while (!hasError()) {
                TokenType op_type = current().type;
                auto bp = infixBindingPower(op_type);
                if (bp.left < minBP) break;

                size_t op_pos = current().pos;
                current_++; // consume operator

                AstNodePtr right = parseExpression(bp.right);
                if (!right) return nullptr;

                BinaryNode bn;
                bn.op    = toBinaryOp(op_type);
                bn.left  = std::move(left);
                bn.right = std::move(right);
                left = std::make_unique<AstNode>(std::move(bn), op_pos);
            }

            return left;
        }

        AstNodePtr parsePrefix() {
            if (hasError()) return nullptr;

            TokenType type = current().type;

            // Unary prefix operators
            int bp = prefixBindingPower(type);
            if (bp >= 0) {
                size_t pos = current().pos;
                current_++; // consume operator
                AstNodePtr operand = parseExpression(bp);
                if (!operand) return nullptr;

                UnaryNode un;
                un.op = toUnaryOp(type);
                un.operand = std::move(operand);
                return std::make_unique<AstNode>(std::move(un), pos);
            }

            return parseAtom();
        }

        // ── Atom parsing ────────────────────────────────────────────

        AstNodePtr parseAtom() {
            if (hasError()) return nullptr;

            TokenType type = current().type;
            size_t pos = current().pos;

            // Parenthesized expression
            if (type == TokenType::LPAREN) {
                current_++; // consume '('
                auto expr = parseExpression(0);
                if (!expr) return nullptr;
                consume(TokenType::RPAREN, "expected ')' after expression");
                return expr;
            }

            // Boolean literals
            if (type == TokenType::TRUE_LIT) {
                current_++;
                LiteralNode ln;
                ln.value = true;
                return std::make_unique<AstNode>(std::move(ln), pos);
            }
            if (type == TokenType::FALSE_LIT) {
                current_++;
                LiteralNode ln;
                ln.value = false;
                return std::make_unique<AstNode>(std::move(ln), pos);
            }

            // Numeric literals
            if (type == TokenType::INTEGER) {
                Token tok = tokens_[current_]; current_++;
                LiteralNode ln;
                // Use uint64_t for values that exceed INT64_MAX (R2)
                if (tok.uint_value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    ln.value = tok.uint_value;
                } else {
                    ln.value = tok.int_value;
                }
                return std::make_unique<AstNode>(std::move(ln), pos);
            }
            if (type == TokenType::FLOAT) {
                Token tok = tokens_[current_]; current_++;
                LiteralNode ln;
                ln.value = tok.float_value;
                return std::make_unique<AstNode>(std::move(ln), pos);
            }

            // String literal
            if (type == TokenType::STRING) {
                Token tok = tokens_[current_]; current_++;
                LiteralNode ln;
                ln.value = std::move(tok.text);
                return std::make_unique<AstNode>(std::move(ln), pos);
            }

            // Cell reference: X[row_offset][col_spec]
            if (type == TokenType::IDENT_X) {
                return parseCellRef();
            }

            error("expected expression");
            return nullptr;
        }

        // ── Cell reference ──────────────────────────────────────────

        AstNodePtr parseCellRef() {
            size_t pos = current().pos;
            current_++; // consume 'X'

            consume(TokenType::LBRACKET, "expected '[' after 'X'");
            if (hasError()) return nullptr;

            // Parse row offset (integer, possibly negative)
            bool negative = false;
            if (match(TokenType::MINUS)) negative = true;
            else if (match(TokenType::PLUS)) {}  // explicit positive

            if (!check(TokenType::INTEGER)) {
                error("expected integer row offset");
                return nullptr;
            }
            int64_t offset = tokens_[current_].int_value;
            if (negative) offset = -offset;
            current_++;

            consume(TokenType::RBRACKET, "expected ']' after row offset");
            if (hasError()) return nullptr;

            // Optional second bracket: [col_spec]
            if (match(TokenType::LBRACKET)) {
                // Wildcard?
                if (match(TokenType::STAR)) {
                    consume(TokenType::RBRACKET, "expected ']' after '*'");
                    if (hasError()) return nullptr;
                    CellRef cr;
                    cr.row_offset  = static_cast<int16_t>(offset);
                    cr.is_wildcard = true;
                    return std::make_unique<AstNode>(std::move(cr), pos);
                }

                // Column name (string)?
                if (check(TokenType::STRING)) {
                    std::string name = tokens_[current_].text;
                    current_++;
                    consume(TokenType::RBRACKET, "expected ']' after column name");
                    if (hasError()) return nullptr;
                    CellRef cr;
                    cr.row_offset = static_cast<int16_t>(offset);
                    cr.is_name    = true;
                    cr.col_name   = std::move(name);
                    return std::make_unique<AstNode>(std::move(cr), pos);
                }

                // Column index (integer)
                if (!check(TokenType::INTEGER)) {
                    error("expected column index, column name, or '*'");
                    return nullptr;
                }
                uint16_t col = static_cast<uint16_t>(tokens_[current_].int_value);
                current_++;
                consume(TokenType::RBRACKET, "expected ']' after column index");
                if (hasError()) return nullptr;

                CellRef cr;
                cr.row_offset = static_cast<int16_t>(offset);
                cr.col_index  = col;
                return std::make_unique<AstNode>(std::move(cr), pos);
            }

            // No column spec → whole row reference (only valid in selection)
            RowRef rr;
            rr.row_offset = static_cast<int16_t>(offset);
            return std::make_unique<AstNode>(std::move(rr), pos);
        }
    };

} // namespace bcsv
