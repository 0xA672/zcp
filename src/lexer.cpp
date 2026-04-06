#include "lexer.hpp"
#include <cctype>
#include <string>

Lexer::Lexer(std::string_view input) : input_(input) {}

char Lexer::peekChar() const {
    if (pos_ < input_.size()) return input_[pos_];
    return '\0';
}

char Lexer::bump() {
    if (pos_ < input_.size()) return input_[pos_++];
    return '\0';
}

void Lexer::skipWhitespace() {
    while (std::isspace(static_cast<unsigned char>(peekChar()))) {
        bump();
    }
}

bool Lexer::skipComment() {
    if (peekChar() != '/') return false;
    char next = pos_ + 1 < input_.size() ? input_[pos_ + 1] : '\0';
    if (next == '/') {
        // line comment
        bump(); bump();
        while (peekChar() != '\0' && peekChar() != '\n') bump();
        return true;
    } else if (next == '*') {
        // block comment and possibly nested
        bump(); bump();
        int depth = 1;
        while (depth > 0) {
            if (peekChar() == '\0') break;
            if (peekChar() == '*') {
                bump();
                if (peekChar() == '/') {
                    bump();
                    depth--;
                }
            } else if (peekChar() == '/') {
                bump();
                if (peekChar() == '*') {
                    bump();
                    depth++;
                }
            } else {
                bump();
            }
        }
        return true;
    }
    return false;
}

std::string_view Lexer::readIdent() {
    size_t start = pos_;
    // ZON allows identifier to start with dot, e.g. .name
    if (peekChar() == '.') bump();
    while (std::isalnum(static_cast<unsigned char>(peekChar())) || peekChar() == '_') {
        bump();
    }
    return input_.substr(start, pos_ - start);
}

std::string_view Lexer::readString() {
    bump(); // consume opening "
    size_t start = pos_;
    while (peekChar() != '\0') {
        if (peekChar() == '"') {
            std::string_view result = input_.substr(start, pos_ - start);
            bump(); // consume closing "
            return result;
        }
        if (peekChar() == '\\') {
            throw std::runtime_error("Escape sequences not supported in zero-copy mode");
        }
        bump();
    }
    throw std::runtime_error("Unclosed string");
}

double Lexer::readNumber() {
    size_t start = pos_;
    if (peekChar() == '-') bump();
    while (std::isdigit(static_cast<unsigned char>(peekChar()))) bump();
    if (peekChar() == '.') {
        bump();
        while (std::isdigit(static_cast<unsigned char>(peekChar()))) bump();
    }
    if (peekChar() == 'e' || peekChar() == 'E') {
        bump();
        if (peekChar() == '+' || peekChar() == '-') bump();
        while (std::isdigit(static_cast<unsigned char>(peekChar()))) bump();
    }
    std::string_view slice = input_.substr(start, pos_ - start);
    return std::stod(std::string(slice));
}

Token Lexer::nextToken() {
    while (true) {
        skipWhitespace();
        if (!skipComment()) break;
    }

    char c = peekChar();
    if (c == '\0') return Token(TokenKind::Eof);

    switch (c) {
        case '{': bump(); return Token(TokenKind::LBrace);
        case '}': bump(); return Token(TokenKind::RBrace);
        case '[': bump(); return Token(TokenKind::LBracket);
        case ']': bump(); return Token(TokenKind::RBracket);
        case ':': bump(); return Token(TokenKind::Colon);
        case ';': bump(); return Token(TokenKind::Semicolon);
        case ',': bump(); return Token(TokenKind::Comma);
        case '=': bump(); return Token(TokenKind::Equal);
        case '.': {
            bump();
            if (peekChar() == '{') {
                bump();
                return Token(TokenKind::DotLBrace);
            } else if (std::isalpha(static_cast<unsigned char>(peekChar())) || peekChar() == '_') {
                std::string_view ident = readIdent();
                return Token(TokenKind::Ident, ident);
            } else {
                throw std::runtime_error("Unexpected '.'");
            }
        }
        case '"': {
            std::string_view s = readString();
            return Token(s);
        }
        default:
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::string_view ident = readIdent();
                if (ident == "true") return Token(true);
                if (ident == "false") return Token(false);
                if (ident == "null") return Token(TokenKind::Null);
                if (ident == "const") return Token(TokenKind::Const);
                return Token(TokenKind::Ident, ident);
            } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
                double num = readNumber();
                return Token(num);
            } else {
                throw std::runtime_error(std::string("Unexpected byte: ") + c);
            }
    }
}
