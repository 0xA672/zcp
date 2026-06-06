#include "lexer.hpp"
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstring>
#include <stdexcept>
#include <string>

// -- Constructor -------------------------------------------------------------

Lexer::Lexer(std::string_view in) : in_(in), pos_(0), line_(1), col_(1) {
    // Skip UTF-8 BOM (EF BB BF) if present
    if (in_.size() >= 3 &&
        (unsigned char)in_[0] == 0xEF &&
        (unsigned char)in_[1] == 0xBB &&
        (unsigned char)in_[2] == 0xBF) {
        pos_ = 3;
    }
}

// -- Helpers -----------------------------------------------------------------

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

void Lexer::skip_ws_and_comments() {
    while (true) {
        // Skip whitespace
        while (std::isspace((unsigned char)peek())) bump();
        // Skip line comment //
        if (peek() == '/' && peek(1) == '/') {
            bump(); bump();
            while (peek() != '\0' && peek() != '\n') bump();
            continue;
        }
        // Skip block comment /* */
        if (peek() == '/' && peek(1) == '*') {
            bump(); bump();
            int depth = 1;
            while (depth > 0) {
                if (peek() == '\0') err("unclosed block comment");
                if (peek() == '/' && peek(1) == '*') { bump(); bump(); ++depth; }
                else if (peek() == '*' && peek(1) == '/') { bump(); bump(); --depth; }
                else bump();
            }
            continue;
        }
        break;
    }
}

// -- Identifier --------------------------------------------------------------

std::string_view Lexer::rd_identifier() {
    size_t s = pos_;
    while (std::isalnum((unsigned char)peek()) || peek() == '_') bump();
    return in_.substr(s, pos_ - s);
}

// -- String ------------------------------------------------------------------

std::string Lexer::rd_string() {
    bump(); // consume opening "
    std::string out;
    while (true) {
        char c = peek();
        if (c == '\0') err("unclosed string literal");
        if (c == '"') { bump(); break; }
        if (c == '\\') {
            bump(); // consume backslash
            c = bump();
            switch (c) {
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '\'': out.push_back('\''); break;
                case '"':  out.push_back('"'); break;
                case '0':  out.push_back('\0'); break;
                case 'x': {
                    auto hex_val = [](char c) -> int {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = hex_val(bump());
                    int lo = hex_val(bump());
                    if (hi < 0 || lo < 0) err("invalid \\x escape");
                    out.push_back((char)((hi << 4) | lo));
                    break;
                }
                case 'u': {
                    uint32_t cp = rd_unicode_escape();
                    utf8_encode(cp, out);
                    break;
                }
                case '\0': err("unexpected end of input in escape");
                default:   err(std::string("unknown escape: \\") + c);
            }
        } else {
            // Raw UTF-8: decode one codepoint
            unsigned char u = (unsigned char)c;
            if (u < 0x80) {
                out.push_back(c);
                bump();
            } else {
                int extra = 0;
                uint32_t cp;
                if ((u & 0xE0) == 0xC0) { cp = u & 0x1F; extra = 1; }
                else if ((u & 0xF0) == 0xE0) { cp = u & 0x0F; extra = 2; }
                else if ((u & 0xF8) == 0xF0) { cp = u & 0x07; extra = 3; }
                else err("invalid UTF-8 byte");
                bump();
                for (int i = 0; i < extra; ++i) {
                    unsigned char b = (unsigned char)bump();
                    if ((b & 0xC0) != 0x80) err("invalid UTF-8 continuation byte");
                    cp = (cp << 6) | (b & 0x3F);
                }
                utf8_encode(cp, out);
            }
        }
    }
    return out;
}

std::string Lexer::rd_multiline_line() {
    std::string out;
    while (peek() != '\n' && peek() != '\0') {
        out.push_back(bump());
    }
    if (peek() == '\n') out.push_back(bump());
    return out;
}

// -- Char content ------------------------------------------------------------

uint32_t Lexer::rd_char_content() {
    char c = peek();
    if (c == '\0' || c == '\n') err("unclosed character literal");
    if (c != '\\') {
        bump();
        unsigned char u = (unsigned char)c;
        if (u < 0x80) return (uint32_t)u;
        int extra = 0;
        uint32_t cp = 0;
        if ((u & 0xE0) == 0xC0) { cp = u & 0x1F; extra = 1; }
        else if ((u & 0xF0) == 0xE0) { cp = u & 0x0F; extra = 2; }
        else if ((u & 0xF8) == 0xF0) { cp = u & 0x07; extra = 3; }
        else err("invalid UTF-8 byte in char literal");
        for (int i = 0; i < extra; ++i) {
            unsigned char b = (unsigned char)bump();
            if ((b & 0xC0) != 0x80) err("invalid UTF-8 continuation byte");
            cp = (cp << 6) | (b & 0x3F);
        }
        return cp;
    }
    bump(); // consume backslash
    char esc = bump();
    switch (esc) {
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case '0':  return '\0';
        case 'x': {
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex_val(bump());
            int lo = hex_val(bump());
            if (hi < 0 || lo < 0) err("invalid \\x escape in char");
            return (uint32_t)((hi << 4) | lo);
        }
        case 'u': return rd_unicode_escape();
        case '\0': err("unexpected end of input in char escape");
        default:   err(std::string("unknown char escape: \\") + esc);
    }
}

uint32_t Lexer::rd_unicode_escape() {
    if (peek() != '{') err("expected '{' after \\u");
    bump();
    uint32_t cp = 0;
    bool has_digit = false;
    while (peek() != '}') {
        char c = peek();
        if (c == '\0') err("unclosed \\u{}");
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) err(std::string("invalid hex in \\u{}: '") + c + "'");
        cp = (cp << 4) | (uint32_t)v;
        if (cp > 0x10FFFF) err("unicode codepoint out of range");
        bump();
        has_digit = true;
    }
    if (!has_digit) err("empty \\u{}");
    bump(); // consume '}'
    return cp;
}

// -- UTF-8 encoder -----------------------------------------------------------

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
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        throw std::runtime_error("invalid Unicode codepoint");
    }
}

// -- Number ------------------------------------------------------------------

Token Lexer::rd_number(bool negative) {
    std::string raw;
    if (negative) raw.push_back('-');

    // Detect base prefix
    bool is_hex = false, is_bin = false, is_oct = false;
    if (peek() == '0') {
        char nxt = peek(1);
        if (nxt == 'x' || nxt == 'X') { raw.push_back('0'); raw.push_back('x'); bump(); bump(); is_hex = true; }
        else if (nxt == 'b' || nxt == 'B') { raw.push_back('0'); raw.push_back('b'); bump(); bump(); is_bin = true; }
        else if (nxt == 'o' || nxt == 'O') { raw.push_back('0'); raw.push_back('o'); bump(); bump(); is_oct = true; }
    }

    // Collect digit string (strip underscores)
    std::string digits;
    bool is_float = false;

    auto collect_digits = [&](auto is_digit) {
        while (is_digit((unsigned char)peek()) || peek() == '_') {
            if (peek() != '_') digits.push_back(peek());
            bump();
        }
    };

    if (is_hex) {
        collect_digits([](unsigned char c) { return std::isxdigit(c); });
        if (peek() == '.') {
            is_float = true;
            digits.push_back('.');
            bump();
            collect_digits([](unsigned char c) { return std::isxdigit(c); });
        }
        if (peek() == 'p' || peek() == 'P') {
            is_float = true;
            digits.push_back('p');
            bump();
            if (peek() == '+' || peek() == '-') { digits.push_back(peek()); bump(); }
            collect_digits([](unsigned char c) { return std::isdigit(c); });
        }
    } else if (is_bin) {
        collect_digits([](unsigned char c) { return c == '0' || c == '1'; });
    } else if (is_oct) {
        collect_digits([](unsigned char c) { return c >= '0' && c <= '7'; });
    } else {
        // Decimal: reject leading zeros (0 is fine, 0123 is not)
        if (peek() == '0' && std::isdigit((unsigned char)peek(1)))
            err("invalid number: leading zero");
        collect_digits([](unsigned char c) { return std::isdigit(c); });
        if (peek() == '.') {
            is_float = true;
            digits.push_back('.');
            bump();
            collect_digits([](unsigned char c) { return std::isdigit(c); });
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            digits.push_back('e');
            bump();
            if (peek() == '+' || peek() == '-') { digits.push_back(peek()); bump(); }
            collect_digits([](unsigned char c) { return std::isdigit(c); });
        }
    }

    if (digits.empty()) err("invalid number literal");

    // Build raw string
    raw += digits;
    if (raw.empty()) err("empty number");

    Token tok(Tk::Number);
    tok.num_str = raw;

    if (is_float) {
        tok.num_is_float = true;
        // Try to parse as double. For hex floats, GCC's from_chars needs workaround.
        std::string src = (is_hex ? "0x" : "") + digits;
        if (negative) src.insert(src.begin(), '-');
        double val = 0.0;
        auto fmt = is_hex ? std::chars_format::hex : std::chars_format::general;
        auto [ptr, ec] = std::from_chars(src.data(), src.data() + src.size(), val, fmt);
        if (is_hex && (ec != std::errc() || ptr != src.data() + src.size())) {
            // GCC workaround: retry without 0x prefix
            src = digits;
            if (negative) src.insert(src.begin(), '-');
            auto [ptr2, ec2] = std::from_chars(src.data(), src.data() + src.size(), val, fmt);
            if (ec2 != std::errc() || ptr2 != src.data() + src.size())
                err("invalid hex float: " + raw);
        } else if (ec != std::errc()) {
            err("invalid float: " + raw);
        }
        tok.num_f64 = val;
    } else {
        tok.num_is_float = false;
        int base = is_hex ? 16 : is_bin ? 2 : is_oct ? 8 : 10;
        uint64_t val = 0;
        bool overflow = false;
        for (char d : digits) {
            int dv = 0;
            if (d >= '0' && d <= '9') dv = d - '0';
            else if (d >= 'a' && d <= 'f') dv = d - 'a' + 10;
            else if (d >= 'A' && d <= 'F') dv = d - 'A' + 10;
            uint64_t prev = val;
            val = val * (uint64_t)base + (uint64_t)dv;
            if (val < prev) { overflow = true; break; }
        }
        if (overflow) {
            // Big integer -- store raw string
            tok.num_u64 = 0;
        } else {
            tok.num_u64 = val;
        }
    }

    return tok;
}

// -- Main token loop ---------------------------------------------------------

Token Lexer::next() {
    skip_ws_and_comments();

    char c = peek();
    if (c == '\0') return Token(Tk::Eof);

    // Single-char tokens
    switch (c) {
        case '.': bump(); return Token(Tk::Period);
        case '{': bump(); return Token(Tk::Lb);
        case '}': bump(); return Token(Tk::Rb);
        case '[': bump(); return Token(Tk::Lbk);
        case ']': bump(); return Token(Tk::Rbk);
        case '=': bump(); return Token(Tk::Equal);
        case ',': bump(); return Token(Tk::Comma);
        case '-': bump(); return Token(Tk::Minus);
    }

    // String literal
    if (c == '"') {
        Token tok(Tk::String);
        tok.str = rd_string();
        return tok;
    }

    // Multiline string literal (\\\\)
    if (c == '\\' && peek(1) == '\\') {
        bump(); bump(); // consume the two backslashes
        Token tok(Tk::MultiLine);
        tok.str = rd_multiline_line();
        return tok;
    }

    // Character literal
    if (c == '\'') {
        bump();
        uint32_t cp = rd_char_content();
        if (peek() != '\'') err("unclosed character literal");
        bump();
        Token tok(Tk::Char);
        tok.cp = cp;
        return tok;
    }

    // Identifier or keyword (all map to Tk::Identifier in ZON)
    if (std::isalpha((unsigned char)c) || c == '_') {
        Token tok(Tk::Identifier);
        tok.sv = rd_identifier();
        return tok;
    }

    // Number
    if (std::isdigit((unsigned char)c)) {
        return rd_number(false);
    }

    err(std::string("unexpected character '") + c + "'");
    __builtin_unreachable();
}
