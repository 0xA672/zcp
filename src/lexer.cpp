#include "lexer.hpp"
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>

// ── Constructors ─────────────────────────────────────────────────────────────

Lexer::Lexer(std::string_view in) : in_(in), pos_(0), line_(1), col_(1) {
    // Skip UTF-8 BOM (EF BB BF) if present
    if (in_.size() >= 3 &&
        (unsigned char)in_[0] == 0xEF &&
        (unsigned char)in_[1] == 0xBB &&
        (unsigned char)in_[2] == 0xBF) {
        pos_ = 3;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

[[noreturn]] void Lexer::err(const std::string& msg) const {
    throw std::runtime_error(msg + " at " + std::to_string(line_) + ":" + std::to_string(col_));
}

char Lexer::peek(size_t offset) const {
    size_t i = pos_ + offset;
    return i < in_.size() ? in_[i] : '\0';
}

char Lexer::bump() {
    if (pos_ >= in_.size()) return '\0';
    char c = in_[pos_++];
    if (c == '\n') { ++line_; col_ = 1; }
    else ++col_;
    return c;
}

void Lexer::skip_ws() {
    while (std::isspace((unsigned char)peek())) bump();
}

bool Lexer::skip_cmt() {
    if (peek() != '/') return false;
    char nxt = peek(1);
    if (nxt == '/') {
        // Line comment (includes doc comments ///)
        bump(); bump();
        while (peek() != '\0' && peek() != '\n') bump();
        return true;
    }
    if (nxt == '*') {
        // Nested block comment
        bump(); bump();
        int depth = 1;
        while (depth > 0) {
            if (peek() == '\0') err("unclosed block comment");
            if (peek() == '/' && peek(1) == '*') { bump(); bump(); ++depth; }
            else if (peek() == '*' && peek(1) == '/') { bump(); bump(); --depth; }
            else bump();
        }
        return true;
    }
    return false;
}

// ── UTF-8 encoder ─────────────────────────────────────────────────────────────

void Lexer::utf8_encode(uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6)  & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        throw std::runtime_error("invalid Unicode codepoint");
    }
}

// ── Escape helpers ────────────────────────────────────────────────────────────

uint8_t Lexer::rd_hex_byte() {
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int hi = hex_val(bump());
    int lo = hex_val(bump());
    if (hi < 0 || lo < 0) err("invalid \\x escape: expected two hex digits");
    return (uint8_t)((hi << 4) | lo);
}

uint32_t Lexer::rd_unicode_escape() {
    // Expects we have already consumed \u and are sitting on {
    if (peek() != '{') err("expected '{' after \\u");
    bump(); // consume '{'
    uint32_t cp = 0;
    bool has_digit = false;
    while (peek() != '}') {
        char c = peek();
        if (c == '\0') err("unclosed \\u{} escape");
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) err(std::string("invalid hex digit in \\u{}: '") + c + "'");
        cp = (cp << 4) | (uint32_t)v;
        if (cp > 0x10FFFF) err("Unicode codepoint out of range in \\u{}");
        bump();
        has_digit = true;
    }
    if (!has_digit) err("empty \\u{} escape");
    bump(); // consume '}'
    return cp;
}

// Reads one character worth of content (handles escapes), returns codepoint.
// quote is either '"' or '\''.
uint32_t Lexer::rd_char_content(char quote) {
    char c = peek();
    if (c == '\0' || c == '\n') err("unclosed character/string literal");
    if (c != '\\') {
        bump();
        // Raw UTF-8: decode one codepoint
        unsigned char u = (unsigned char)c;
        if (u < 0x80) return (uint32_t)u;
        // Multi-byte UTF-8
        int extra = 0;
        uint32_t cp = 0;
        if ((u & 0xE0) == 0xC0) { cp = u & 0x1F; extra = 1; }
        else if ((u & 0xF0) == 0xE0) { cp = u & 0x0F; extra = 2; }
        else if ((u & 0xF8) == 0xF0) { cp = u & 0x07; extra = 3; }
        else err("invalid UTF-8 byte in literal");
        for (int i = 0; i < extra; ++i) {
            unsigned char b = (unsigned char)bump();
            if ((b & 0xC0) != 0x80) err("invalid UTF-8 continuation byte");
            cp = (cp << 6) | (b & 0x3F);
        }
        return cp;
    }
    // Escape sequence
    bump(); // consume '\'
    char esc = bump();
    switch (esc) {
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case '0':  return '\0';
        case 'x':  return (uint32_t)rd_hex_byte();
        case 'u':  return rd_unicode_escape();
        case '\0': err("unexpected end of input in escape sequence");
        default:   err(std::string("unknown escape sequence: \\") + esc);
    }
}

// ── Identifier ────────────────────────────────────────────────────────────────

std::string_view Lexer::rd_id() {
    size_t s = pos_;
    // caller may have already passed a leading '.' for enum literals
    while (std::isalnum((unsigned char)peek()) || peek() == '_') bump();
    return in_.substr(s, pos_ - s);
}

// ── String ────────────────────────────────────────────────────────────────────

std::string Lexer::rd_str() {
    bump(); // consume opening "
    std::string out;
    while (true) {
        char c = peek();
        if (c == '\0') err("unclosed string literal");
        if (c == '"') { bump(); break; }
        uint32_t cp = rd_char_content('"');
        utf8_encode(cp, out);
    }
    return out;
}

// Multiline string: one or more consecutive \\ lines.
// Each \\ line contributes the content after \\ up to (and including) the newline.
std::string Lexer::rd_multiline_str() {
    std::string out;
    while (peek() == '\\' && peek(1) == '\\') {
        bump(); bump(); // consume \\
        // Collect raw bytes until newline or EOF
        while (peek() != '\n' && peek() != '\0') {
            out.push_back(bump());
        }
        if (peek() == '\n') out.push_back(bump()); // include the newline
        // Skip whitespace-only indent before next \\ line
        size_t saved = pos_;
        size_t saved_line = line_, saved_col = col_;
        while (peek() == ' ' || peek() == '\t') bump();
        if (peek() != '\\' || peek(1) != '\\') {
            // Not another \\ line — restore position (indent belongs to outer context)
            pos_ = saved; line_ = saved_line; col_ = saved_col;
            break;
        }
    }
    return out;
}

// ── Number ────────────────────────────────────────────────────────────────────

Token Lexer::rd_num(bool negative) {
    // Detect base prefix
    bool is_hex = false, is_bin = false, is_oct = false;
    if (peek() == '0') {
        char nxt = peek(1);
        if (nxt == 'x' || nxt == 'X') { bump(); bump(); is_hex = true; }
        else if (nxt == 'b' || nxt == 'B') { bump(); bump(); is_bin = true; }
        else if (nxt == 'o' || nxt == 'O') { bump(); bump(); is_oct = true; }
    }

    // Collect digit string (strip underscores)
    std::string digits;
    bool is_float = false;

    if (is_hex) {
        // Hex integer or hex float (0x<hex>.<hex>p<exp>)
        while (std::isxdigit((unsigned char)peek()) || peek() == '_') {
            if (peek() != '_') digits.push_back(peek());
            bump();
        }
        if (peek() == '.') {
            // Hex float
            is_float = true;
            digits.push_back('.');
            bump();
            while (std::isxdigit((unsigned char)peek()) || peek() == '_') {
                if (peek() != '_') digits.push_back(peek());
                bump();
            }
        }
        if (peek() == 'p' || peek() == 'P') {
            is_float = true;
            digits.push_back('p');
            bump();
            if (peek() == '+' || peek() == '-') { digits.push_back(peek()); bump(); }
            while (std::isdigit((unsigned char)peek()) || peek() == '_') {
                if (peek() != '_') digits.push_back(peek());
                bump();
            }
        }
    } else if (is_bin) {
        while (peek() == '0' || peek() == '1' || peek() == '_') {
            if (peek() != '_') digits.push_back(peek());
            bump();
        }
    } else if (is_oct) {
        while ((peek() >= '0' && peek() <= '7') || peek() == '_') {
            if (peek() != '_') digits.push_back(peek());
            bump();
        }
    } else {
        // Decimal integer or float
        // Reject leading zeros: "0" is fine, "0123" is not
        if (peek() == '0' && std::isdigit((unsigned char)peek(1))) {
            err("invalid number: leading zero");
        }
        while (std::isdigit((unsigned char)peek()) || peek() == '_') {
            if (peek() != '_') digits.push_back(peek());
            bump();
        }
        if (peek() == '.') {
            is_float = true;
            digits.push_back('.');
            bump();
            while (std::isdigit((unsigned char)peek()) || peek() == '_') {
                if (peek() != '_') digits.push_back(peek());
                bump();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            digits.push_back('e');
            bump();
            if (peek() == '+' || peek() == '-') { digits.push_back(peek()); bump(); }
            while (std::isdigit((unsigned char)peek()) || peek() == '_') {
                if (peek() != '_') digits.push_back(peek());
                bump();
            }
        }
    }

    if (digits.empty()) err("invalid number literal");

    // Build token
    Token tok(Tk::Float); // default, may change

    if (is_float) {
        // Parse as double
        std::string full = (is_hex ? "0x" : "") + digits;
        if (negative) full.insert(full.begin(), '-');
        double val = 0.0;
        auto [ptr, ec] = std::from_chars(full.data(), full.data() + full.size(), val,
                                          is_hex ? std::chars_format::hex
                                                 : std::chars_format::general);
        if (ec != std::errc()) err("invalid float literal: " + full);
        tok.kind    = Tk::Float;
        tok.num.kind = NumVal::Kind::F64;
        tok.num.f64  = val;
    } else {
        // Integer: parse in the appropriate base
        uint64_t val = 0;
        bool overflow = false;
        int base = is_hex ? 16 : is_bin ? 2 : is_oct ? 8 : 10;

        for (char d : digits) {
            int dv = 0;
            if (d >= '0' && d <= '9') dv = d - '0';
            else if (d >= 'a' && d <= 'f') dv = d - 'a' + 10;
            else if (d >= 'A' && d <= 'F') dv = d - 'A' + 10;

            uint64_t prev = val;
            val = val * (uint64_t)base + (uint64_t)dv;
            if (val < prev) { overflow = true; break; }
        }

        tok.kind = Tk::Int;
        if (overflow) {
            // Store as decimal string for big-int support
            if (base == 10) {
                tok.num.kind = NumVal::Kind::Big;
                tok.num.big  = (negative ? "-" : "") + digits;
            } else {
                // Non-decimal overflow: convert to decimal string via __int128 if possible,
                // otherwise store hex-prefixed string for caller to handle
                tok.num.kind = NumVal::Kind::Big;
                tok.num.big  = (negative ? "-0x" : "0x") + digits;
            }
        } else {
            tok.num.kind = NumVal::Kind::U64;
            tok.num.u64  = val;
            // Represent negative as Float if the value can't be stored in uint64_t signed
            // ZON integers are unsigned in the literal; sign is handled by parser (-inf, -<num>)
        }
    }

    return tok;
}

// ── Main token loop ───────────────────────────────────────────────────────────

Token Lexer::next() {
    // Skip whitespace and comments
    while (true) {
        skip_ws();
        if (!skip_cmt()) break;
    }

    char c = peek();
    if (c == '\0') return Token(Tk::Eof);

    switch (c) {
        case '{': bump(); return Token(Tk::Lb);
        case '}': bump(); return Token(Tk::Rb);
        case '[': bump(); return Token(Tk::Lbk);
        case ']': bump(); return Token(Tk::Rbk);
        case ':': bump(); return Token(Tk::Colon);
        case ',': bump(); return Token(Tk::Comma);
        case '=': bump(); return Token(Tk::Eq);
        case '-': bump(); return Token(Tk::Minus);

        case '.': {
            char n1 = peek(1);
            char n2 = peek(2);
            if (n1 == '.' && n2 == '.') { bump(); bump(); bump(); return Token(Tk::DotDotDot); }
            if (n1 == '.')              { bump(); bump();          return Token(Tk::DotDot);    }
            if (n1 == '{')             { bump(); bump();          return Token(Tk::DotLb);     }
            if (std::isalpha((unsigned char)n1) || n1 == '_') {
                bump(); // consume '.'
                auto name = rd_id();
                Token tok(Tk::EnumLit);
                tok.sv = name;
                return tok;
            }
            err("unexpected '.'");
        }

        case '"': {
            Token tok(Tk::Str);
            tok.str = rd_str();
            return tok;
        }

        case '\\': {
            // Multiline string starts with \\
            if (peek(1) == '\\') {
                Token tok(Tk::Str);
                tok.str = rd_multiline_str();
                return tok;
            }
            err("unexpected '\\'");
        }

        case '\'': {
            bump(); // consume opening '
            uint32_t cp = rd_char_content('\'');
            if (peek() != '\'') err("unclosed character literal");
            bump(); // consume closing '
            Token tok(Tk::Char);
            tok.cp = cp;
            return tok;
        }

        default:
            // Identifier / keyword
            if (std::isalpha((unsigned char)c) || c == '_') {
                auto id = rd_id();
                if (id == "true")  { Token t(Tk::Bool); t.b = true;  return t; }
                if (id == "false") { Token t(Tk::Bool); t.b = false; return t; }
                if (id == "null")  return Token(Tk::Null);
                if (id == "nan")   return Token(Tk::Nan);
                if (id == "inf")   return Token(Tk::Inf);
                Token tok(Tk::Id);
                tok.sv = id;
                return tok;
            }
            // Number
            if (std::isdigit((unsigned char)c)) {
                return rd_num(false);
            }
            err(std::string("unexpected character '") + c + "'");
    }
}
