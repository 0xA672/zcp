#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <stdexcept>

// Token kinds for ZON (Zig Object Notation)
enum class Tk {
    Str,        // "..."  or  \\ multiline string  (value: std::string, UTF-8)
    Int,        // integer literal (value: uint64_t, or std::string for big-int)
    Float,      // float  literal (value: double)
    Char,       // 'x'    character literal (value: uint32_t codepoint)
    Bool,       // true / false (value: bool via std::monostate — kind alone encodes it; use Token::b)
    Null,       // null
    Nan,        // nan
    Inf,        // inf  (always positive; negate at parser level for -inf)
    DotLb,      // .{
    DotDot,     // ..
    DotDotDot,  // ...
    Lb,         // {
    Rb,         // }
    Lbk,        // [
    Rbk,        // ]
    Colon,      // :
    Comma,      // ,
    Eq,         // =
    Minus,      // -   (unary minus; parser handles -inf / -nan / -<number>)
    Id,         // bare identifier
    EnumLit,    // .Name  (enum literal; value is the name without the leading dot)
    Eof,
};

// Numeric value: either a small integer (fits uint64_t), a big integer stored as
// decimal digit string, or a double.
struct NumVal {
    enum class Kind { U64, Big, F64 } kind;
    uint64_t    u64 = 0;
    std::string big;   // non-empty only when kind == Big
    double      f64 = 0.0;
};

struct Token {
    Tk  kind;

    // Payload — only the relevant field is valid for each Tk:
    //   Str      -> str
    //   Int      -> num (kind U64 or Big)
    //   Float    -> num (kind F64)
    //   Char     -> cp  (Unicode codepoint)
    //   Bool     -> b
    //   Id       -> sv  (points into original source)
    //   EnumLit  -> sv  (name without dot, points into original source)
    //   all others: no payload
    std::string      str;
    NumVal           num;
    uint32_t         cp  = 0;
    bool             b   = false;
    std::string_view sv;

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
    void    skip_ws();
    bool    skip_cmt();

    std::string_view rd_id();
    std::string      rd_str();
    std::string      rd_multiline_str();
    Token            rd_num(bool negative);
    uint32_t         rd_char_content(char quote);  // reads one escape or raw codepoint
    uint32_t         rd_unicode_escape();           // \u{H...}
    uint8_t          rd_hex_byte();                 // \xHH -> single byte value

    // Encode a Unicode codepoint to UTF-8 and append to out
    static void utf8_encode(uint32_t cp, std::string& out);
};
