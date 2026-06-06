#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <stdexcept>

// Token kinds for ZON (Zig Object Notation) -- matching Zig 0.16 std.zig.Ast
enum class Tk {
    Period,       // .
    Lb,           // {
    Rb,           // }
    Lbk,          // [
    Rbk,          // ]
    Equal,        // =
    Comma,        // ,
    Minus,        // -  (unary, for negative numbers)
    Identifier,   // includes keywords: true, false, null, nan, inf
    Number,       // all numeric literals: int, float, hex, bin, oct, sci, big
    String,       // "..."
    Char,         // 'x'
    MultiLine,    // \\ line content
    Eof,
};

struct Token {
    Tk kind;
    std::string       str;       // String content (without quotes)
    std::string       num_str;   // Raw number literal text (for parser to decode)
    uint64_t          num_u64;   // Parsed integer value (if fits u64)
    double            num_f64;   // Parsed float value
    bool              num_is_float = false; // true = float, false = integer
    uint32_t          cp = 0;    // Char codepoint
    std::string_view  sv;        // Identifier name (points into source)

    explicit Token(Tk k) : kind(k) {}
};

class Lexer {
public:
    explicit Lexer(std::string_view in);
    Token next();

private:
    std::string_view in_;
    size_t pos_;
    size_t line_;
    size_t col_;

    [[noreturn]] void err(const std::string& msg) const;
    char    peek(size_t offset = 0) const;
    char    bump();
    void    skip_ws_and_comments();

    std::string_view rd_identifier();
    Token            rd_number(bool negative);
    std::string      rd_string();
    std::string      rd_multiline_line();
    uint32_t         rd_char_content();  // reads one escape or raw codepoint
    uint32_t         rd_unicode_escape();

    static void utf8_encode(uint32_t cp, std::string& out);
};
