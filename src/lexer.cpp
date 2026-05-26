#include "lexer.hpp"
#include <cctype>
#include <charconv>
#include <stdexcept>
#include <string>

Token::Token(Tk k) : kind(k), val(false) {}
Token::Token(std::string s) : kind(Tk::Str), val(std::move(s)) {}
Token::Token(double n) : kind(Tk::Num), val(n) {}
Token::Token(bool b) : kind(Tk::Bool), val(b) {}
Token::Token(Tk k, std::string_view id) : kind(k), val(id) {}

Lexer::Lexer(std::string_view in) : in_(in), pos_(0), line_(1), col_(1) {}

Token Lexer::next() {
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
        case ';': bump(); return Token(Tk::Semi);
        case ',': bump(); return Token(Tk::Comma);
        case '=': bump(); return Token(Tk::Eq);
        case '.': {
            char nxt = pos_ + 1 < in_.size() ? in_[pos_ + 1] : '\0';
            if (nxt == '{') {
                bump(); bump();
                return Token(Tk::DotLb);
            }
            if (std::isalpha(static_cast<unsigned char>(nxt)) || nxt == '_') {
                auto id = rd_id();
                return Token(Tk::EnumLit, id);
            }
            err("unexpected '.'");
        }
        case '"': {
            auto s = rd_str();
            return Token(std::string(s));
        }
        default:
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                auto id = rd_id();
                if (id == "true") return Token(true);
                if (id == "false") return Token(false);
                if (id == "null") return Token(Tk::Null);
                if (id == "const") return Token(Tk::Const);
                return Token(Tk::Id, id);
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
                double n = rd_num();
                return Token(n);
            }
            err(std::string("unexpected char '") + c + "'");
    }
}

[[noreturn]] void Lexer::err(const std::string& msg) const {
    throw std::runtime_error(msg + " at " + std::to_string(line_) + ":" + std::to_string(col_));
}

char Lexer::peek() const {
    return pos_ < in_.size() ? in_[pos_] : '\0';
}

char Lexer::bump() {
    if (pos_ >= in_.size()) return '\0';
    char c = in_[pos_++];
    if (c == '\n') { line_++; col_ = 1; }
    else col_++;
    return c;
}

void Lexer::skip_ws() {
    while (std::isspace(static_cast<unsigned char>(peek()))) bump();
}

bool Lexer::skip_cmt() {
    if (peek() != '/') return false;
    char nxt = pos_ + 1 < in_.size() ? in_[pos_ + 1] : '\0';
    if (nxt == '/') {
        bump(); bump();
        while (peek() != '\0' && peek() != '\n') bump();
        return true;
    }
    if (nxt == '*') {
        bump(); bump();
        int d = 1;
        while (d > 0) {
            if (peek() == '\0') err("unclosed block comment");
            if (peek() == '*') {
                bump();
                if (peek() == '/') { bump(); d--; }
            } else if (peek() == '/') {
                bump();
                if (peek() == '*') { bump(); d++; }
            } else {
                bump();
            }
        }
        return true;
    }
    return false;
}

std::string_view Lexer::rd_id() {
    size_t s = pos_;
    if (peek() == '.') bump();
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') bump();
    return in_.substr(s, pos_ - s);
}

std::string Lexer::rd_str() {
    bump();
    std::string v;
    size_t s = pos_;
    while (true) {
        char c = peek();
        if (c == '\0') err("unclosed string");
        if (c == '"') {
            if (v.empty()) {
                auto r = in_.substr(s, pos_ - s);
                bump();
                return std::string(r);
            }
            v.append(in_.data() + s, pos_ - s);
            bump();
            return v;
        }
        if (c == '\\') {
            v.append(in_.data() + s, pos_ - s);
            bump();
            char esc = bump();
            switch (esc) {
                case '"':  v.push_back('"'); break;
                case '\\': v.push_back('\\'); break;
                case 'n':  v.push_back('\n'); break;
                case 'r':  v.push_back('\r'); break;
                case 't':  v.push_back('\t'); break;
                case '\0': err("unclosed escape");
                default:   err(std::string("unknown escape \\") + esc);
            }
            s = pos_;
        } else {
            bump();
        }
    }
}

double Lexer::rd_num() {
    size_t s = pos_;
    if (peek() == '-') bump();

    bool is_hex = false;
    if (peek() == '0') {
        char nxt = pos_ + 1 < in_.size() ? in_[pos_ + 1] : '\0';
        if (nxt == 'x' || nxt == 'X') {
            bump(); bump();
            is_hex = true;
        }
    }

    std::string raw;
    while (true) {
        char c = peek();
        if (c == '_') { bump(); continue; }
        if (is_hex) {
            if (std::isxdigit(static_cast<unsigned char>(c))) {
                raw.push_back(c);
            } else {
                break;
            }
        } else {
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                raw.push_back(c);
            } else if (c == 'e' || c == 'E') {
                raw.push_back('e');
                bump();
                if (peek() == '+' || peek() == '-') {
                    raw.push_back(peek());
                    bump();
                }
                while (peek() == '_' ||
                       std::isdigit(static_cast<unsigned char>(peek()))) {
                    if (peek() == '_') { bump(); continue; }
                    raw.push_back(peek());
                    bump();
                }
                break;
            } else {
                break;
            }
        }
        bump();
    }

    if (raw.empty()) err("invalid number");

    if (is_hex) {
        raw.insert(0, "0x");
        unsigned long long v = std::stoull(raw, nullptr, 16);
        double result = static_cast<double>(v);
        if (s > 0 && in_[s-1] == '-') result = -result;
        return result;
    }

    if (s > 0 && in_[s-1] == '-') raw.insert(raw.begin(), '-');
    double val = 0.0;
    auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), val);
    if (ec != std::errc()) err("invalid number");
    return val;
}
