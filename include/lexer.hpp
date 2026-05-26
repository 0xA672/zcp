#pragma once

#include <string_view>
#include <variant>
#include <string>
#include <stdexcept>

enum class Tk {
    Str,
    Num,
    Bool,
    Null,
    DotLb,
    Lb,
    Rb,
    Lbk,
    Rbk,
    Colon,
    Comma,
    Eq,
    Id,
    EnumLit,
    Const,
    Eof,
    Semi
};

struct Token {
    Tk kind;
    std::variant<std::string, double, bool, std::string_view> val;

    Token(Tk k);
    Token(std::string s);
    Token(double n);
    Token(bool b);
    Token(Tk k, std::string_view id);
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
    char peek() const;
    char bump();
    void skip_ws();
    bool skip_cmt();
    std::string_view rd_id();
    std::string rd_str();
    double rd_num();
};
