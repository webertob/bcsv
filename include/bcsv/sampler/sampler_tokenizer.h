/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bcsv {

    // ── Token Types ─────────────────────────────────────────────────

    enum class TokenType : uint8_t {
        // Literals
        INTEGER,        // 42, -7
        FLOAT,          // 3.14, 1.0e-5
        STRING,         // "hello"
        TRUE_LIT,       // true
        FALSE_LIT,      // false

        // Identifiers / keywords
        IDENT_X,        // X (cell reference prefix)

        // Delimiters
        LBRACKET,       // [
        RBRACKET,       // ]
        LPAREN,         // (
        RPAREN,         // )
        COMMA,          // ,
        STAR,           // * (wildcard)

        // Arithmetic operators
        PLUS,           // +
        MINUS,          // -
        SLASH,          // /
        PERCENT,        // %

        // Comparison operators
        EQ,             // ==
        NE,             // !=
        LT,             // <
        LE,             // <=
        GT,             // >
        GE,             // >=

        // Boolean operators
        AND,            // &&
        OR,             // ||
        BANG,           // !
        TILDE,          // ~

        // Bitwise operators
        AMP,            // &  (bitwise AND — distinct from &&)
        PIPE,           // |  (bitwise OR  — distinct from ||)
        CARET,          // ^  (bitwise XOR)
        SHL,            // <<
        SHR,            // >>

        // End
        END_OF_INPUT,
        ERROR,          // tokenizer error (message in token text)
    };

    // ── Token ───────────────────────────────────────────────────────

    struct Token {
        TokenType   type = TokenType::END_OF_INPUT;
        size_t      pos  = 0;           // character offset in source
        std::string text;               // raw text of the token

        // Convenience: parsed numeric values (valid when type matches)
        int64_t     int_value   = 0;
        uint64_t    uint_value  = 0;
        double      float_value = 0.0;
    };

    // ── Tokenizer ───────────────────────────────────────────────────
    //
    // Stateless — call tokenize() to produce a complete token stream.
    // All tokens include their source position for error reporting.

    class Tokenizer {
    public:
        /// Tokenize an expression string.  Returns a vector of tokens
        /// ending with END_OF_INPUT (or ERROR on failure).
        static std::vector<Token> tokenize(std::string_view source) {
            Tokenizer t(source);
            std::vector<Token> tokens;
            while (true) {
                Token tok = t.nextToken();
                TokenType ty = tok.type;
                tokens.push_back(std::move(tok));
                if (ty == TokenType::END_OF_INPUT || ty == TokenType::ERROR)
                    break;
            }
            return tokens;
        }

    private:
        std::string_view src_;
        size_t           pos_ = 0;

        explicit Tokenizer(std::string_view source) : src_(source) {}

        // ── Character helpers ───────────────────────────────────────

        bool atEnd() const { return pos_ >= src_.size(); }

        char peek() const { return atEnd() ? '\0' : src_[pos_]; }

        char peekNext() const {
            return (pos_ + 1 < src_.size()) ? src_[pos_ + 1] : '\0';
        }

        char advance() { return src_[pos_++]; }

        void skipWhitespace() {
            while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())))
                advance();
        }

        // ── Token constructors ──────────────────────────────────────

        Token makeToken(TokenType type, size_t start) const {
            Token t;
            t.type = type;
            t.pos  = start;
            t.text = std::string(src_.substr(start, pos_ - start));
            return t;
        }

        Token makeError(const std::string& msg, size_t start) const {
            Token t;
            t.type = TokenType::ERROR;
            t.pos  = start;
            t.text = msg;
            return t;
        }

        // ── Number parsing ──────────────────────────────────────────

        Token scanNumber(size_t start) {
            // Collect digits
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
                advance();

            bool is_float = false;

            // Decimal point?
            if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
                is_float = true;
                advance(); // consume '.'
                while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
                    advance();
            }

            // Exponent?
            if (peek() == 'e' || peek() == 'E') {
                is_float = true;
                advance();
                if (peek() == '+' || peek() == '-')
                    advance();
                if (!std::isdigit(static_cast<unsigned char>(peek())))
                    return makeError("invalid number: expected digit after exponent", start);
                while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
                    advance();
            }

            Token tok = makeToken(is_float ? TokenType::FLOAT : TokenType::INTEGER, start);

            // Parse the value
            if (is_float) {
                try { tok.float_value = std::stod(tok.text); }
                catch (...) { return makeError("invalid float literal", start); }
            } else {
                // Try unsigned first (all non-negative integer literals)
                try {
                    tok.uint_value = std::stoull(tok.text);
                    tok.int_value  = static_cast<int64_t>(tok.uint_value);
                } catch (...) {
                    return makeError("integer literal out of range", start);
                }
            }

            return tok;
        }

        // ── Hex literal parsing ─────────────────────────────────────

        Token scanHexNumber(size_t start) {
            advance(); // consume 'x' or 'X' (0 already consumed)
            if (!std::isxdigit(static_cast<unsigned char>(peek())))
                return makeError("invalid hex literal: expected hex digit after '0x'", start);

            while (!atEnd() && std::isxdigit(static_cast<unsigned char>(peek())))
                advance();

            Token tok = makeToken(TokenType::INTEGER, start);
            try {
                tok.uint_value = std::stoull(tok.text, nullptr, 16);
                tok.int_value  = static_cast<int64_t>(tok.uint_value);
            } catch (...) {
                return makeError("hex literal out of range", start);
            }
            return tok;
        }

        // ── String parsing ──────────────────────────────────────────

        Token scanString(size_t start) {
            advance(); // consume opening '"'
            std::string value;
            while (!atEnd() && peek() != '"') {
                if (peek() == '\\') {
                    advance();
                    if (atEnd())
                        return makeError("unterminated string escape", start);
                    switch (peek()) {
                        case '"':  value += '"';  break;
                        case '\\': value += '\\'; break;
                        case 'n':  value += '\n'; break;
                        case 't':  value += '\t'; break;
                        default:
                            return makeError(
                                std::string("unknown escape sequence: \\") + peek(), start);
                    }
                    advance();
                } else {
                    value += advance();
                }
            }
            if (atEnd())
                return makeError("unterminated string literal", start);
            advance(); // consume closing '"'

            Token tok;
            tok.type = TokenType::STRING;
            tok.pos  = start;
            tok.text = std::move(value);
            return tok;
        }

        // ── Main dispatch ───────────────────────────────────────────

        Token nextToken() {
            skipWhitespace();
            if (atEnd())
                return makeToken(TokenType::END_OF_INPUT, pos_);

            size_t start = pos_;
            char c = advance();

            // Single-character tokens
            switch (c) {
                case '[': return makeToken(TokenType::LBRACKET, start);
                case ']': return makeToken(TokenType::RBRACKET, start);
                case '(': return makeToken(TokenType::LPAREN,   start);
                case ')': return makeToken(TokenType::RPAREN,   start);
                case ',': return makeToken(TokenType::COMMA,    start);
                case '+': return makeToken(TokenType::PLUS,     start);
                case '-': return makeToken(TokenType::MINUS,    start);
                case '/': return makeToken(TokenType::SLASH,    start);
                case '%': return makeToken(TokenType::PERCENT,  start);
                case '~': return makeToken(TokenType::TILDE,    start);
                case '^': return makeToken(TokenType::CARET,    start);
                case '*': return makeToken(TokenType::STAR,     start);
                default: break;
            }

            // Two-character tokens
            if (c == '=' && peek() == '=') { advance(); return makeToken(TokenType::EQ, start); }
            if (c == '!' && peek() == '=') { advance(); return makeToken(TokenType::NE, start); }
            if (c == '!') return makeToken(TokenType::BANG, start);

            if (c == '<' && peek() == '=') { advance(); return makeToken(TokenType::LE, start); }
            if (c == '<' && peek() == '<') { advance(); return makeToken(TokenType::SHL, start); }
            if (c == '<') return makeToken(TokenType::LT, start);

            if (c == '>' && peek() == '=') { advance(); return makeToken(TokenType::GE, start); }
            if (c == '>' && peek() == '>') { advance(); return makeToken(TokenType::SHR, start); }
            if (c == '>') return makeToken(TokenType::GT, start);

            if (c == '&' && peek() == '&') { advance(); return makeToken(TokenType::AND, start); }
            if (c == '&') return makeToken(TokenType::AMP, start);

            if (c == '|' && peek() == '|') { advance(); return makeToken(TokenType::OR, start); }
            if (c == '|') return makeToken(TokenType::PIPE, start);

            // String literal
            if (c == '"') {
                pos_ = start; // reset — scanString expects to consume the '"'
                return scanString(start);
            }

            // Number literal
            if (std::isdigit(static_cast<unsigned char>(c))) {
                // Check for hex: 0x...
                if (c == '0' && (peek() == 'x' || peek() == 'X')) {
                    return scanHexNumber(start);
                }
                return scanNumber(start);
            }

            // Identifiers: X, true, false
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                while (!atEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
                    advance();
                std::string_view word = src_.substr(start, pos_ - start);

                if (word == "X")     return makeToken(TokenType::IDENT_X,   start);
                if (word == "true")  return makeToken(TokenType::TRUE_LIT,  start);
                if (word == "false") return makeToken(TokenType::FALSE_LIT, start);

                return makeError("unknown identifier: " + std::string(word), start);
            }

            return makeError(std::string("unexpected character: ") + c, start);
        }
    };

} // namespace bcsv
