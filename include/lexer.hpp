#pragma once

#include <string_view>
#include <variant>
#include <optional>
#include <stdexcept>

enum class TokenKind {
    String,
    Number,
    Bool,
    Null,
    DotLBrace,   // .{
    LBrace,      // {
    RBrace,      // }
    LBracket,    // [
    RBracket,    // ]
    Colon,       // :
    Comma,       // ,
    Equal,       // =
    Ident,
    Const,
    Eof,
    Semicolon    // ;
};

struct Token {
    TokenKind kind;
    std::variant<std::string_view, double, bool> value;

    Token(TokenKind k) : kind(k), value(false) {}
    Token(std::string_view s) : kind(TokenKind::String), value(s) {}
    Token(double n) : kind(TokenKind::Number), value(n) {}
    Token(bool b) : kind(TokenKind::Bool), value(b) {}
    Token(TokenKind k, std::string_view id) : kind(k), value(id) {
    }
};

class Lexer {
public:
    explicit Lexer(std::string_view input);

    Token nextToken();  // throws std::runtime_error

private:
    std::string_view input_;
    size_t pos_ = 0;

    char peekChar() const;
    char bump();
    void skipWhitespace();
    bool skipComment();
    std::string_view readIdent();
    std::string_view readString();
    double readNumber();
};
