#ifndef ZCP_PARSER_HPP
#define ZCP_PARSER_HPP

#include "lexer.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>

// -- AST node types-----------------------------------------------------------------

struct AstNode {
    enum class Kind {
        Struct,     // .{ .key = val, ... }  (named struct)
        Tuple,      // .{ val, val, ... }    (anonymous tuple)
        Array,      // [ val, val, ... ]
        String,     // "..."
        Number,  // all numeric literals
        Bool,       // true / false
        Null,       // null
        Nan,        // nan
        Inf,        // inf
        EnumLit,    // .foo  (period + identifier)
        Identifier,  // bare identifier
        Char,       // 'x'
        MultiLine,  // \\... multiline string line
    };
    Kind kind;
    std::string str_val;          // String content, EnumLit name, big-int raw, MultiLine content
    int64_t     int_val = 0;  // Integer value (0 if overflow/big)
    double      float_val = 0.0;  // Float value
    bool        is_float = false;  // Number is float vs int
    bool        bool_val = false;  // Bool value
    uint32_t    cp = 0;  // Char codepoint

    // Struct fields: ordered list of (key, value) pairs
    // key is empty for tuple elements
    std::vector<std::pair<std::string, std::unique_ptr<AstNode>>> fields;

    // Array/tuple items
    std::vector<std::unique_ptr<AstNode>> items;
};

// -- Parser----------------------------------------------------------------------

class Parser {
public:
    explicit Parser(std::string_view input) : lex_(input), tok_(Tk::Eof) {
        advance();
    }

    std::unique_ptr<AstNode> parse() {
        return parse_value();
    }

    const std::string& error() const { return error_; }
    bool has_error() const { return !error_.empty(); }

private:
    Lexer lex_;
    Token tok_;
    std::string error_;

    void advance() { tok_ = lex_.next(); }

    std::unique_ptr<AstNode> parse_value() {
        switch (tok_.kind) {
            case Tk::Period: {
                advance();
                if (tok_.kind == Tk::Lb) {
                    // .{ ... }  -- struct or tuple
                    advance();
                    return parse_struct_or_tuple();
                }
                if (tok_.kind == Tk::Identifier) {
                    // .foo -- enum literal
                    auto node = std::make_unique<AstNode>();
                    node->kind = AstNode::Kind::EnumLit;
                    node->str_val = std::string(tok_.sv);
                    advance();
                    return node;
                }
                // trailing period at end of struct .}
                // handled at call site
                error_ = "unexpected '.'";
                return nullptr;
            }
            case Tk::Lb: {
                // bare { } -- treat as tuple without dot (some ZON variants)
                advance();
                return parse_struct_or_tuple();
            }
            case Tk::Lbk: {
                advance();
                return parse_array();
            }
            case Tk::String: {
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::String;
                node->str_val = tok_.str;
                advance();
                return node;
            }
            case Tk::Number: {
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::Number;
                node->is_float = tok_.num_is_float;
                if (tok_.num_is_float) {
                    node->float_val = tok_.num_f64;
                } else {
                    node->int_val = tok_.num_u64;
                    // Preserve raw string for big ints
                    if (tok_.is_big_int) {
                        node->str_val = tok_.num_str;  // big integer
                    }
                }
                advance();
                return node;
            }
            case Tk::Identifier: {
                std::string_view id = tok_.sv;
                advance();
                // Keywords are identifiers in ZON, resolve them here
                if (id == "true")  { auto n = std::make_unique<AstNode>(); n->kind = AstNode::Kind::Bool; n->bool_val = true; return n; }
                if (id == "false") { auto n = std::make_unique<AstNode>(); n->kind = AstNode::Kind::Bool; n->bool_val = false; return n; }
                if (id == "null")  { auto n = std::make_unique<AstNode>(); n->kind = AstNode::Kind::Null; return n; }
                if (id == "nan")   { auto n = std::make_unique<AstNode>(); n->kind = AstNode::Kind::Nan; return n; }
                if (id == "inf")   { auto n = std::make_unique<AstNode>(); n->kind = AstNode::Kind::Inf; return n; }
                // Bare identifier
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::Identifier;
                node->str_val = std::string(id);
                return node;
            }
            case Tk::Char: {
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::Char;
                node->cp = tok_.cp;
                advance();
                return node;
            }
            case Tk::MultiLine: {
                // May have multiple consecutive backslash lines.
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::MultiLine;
                node->str_val = tok_.str;
                advance();
                // Concatenate adjacent multiline lines
                while (tok_.kind == Tk::MultiLine) {
                    node->str_val += tok_.str;
                    advance();
                }
                return node;
            }
            case Tk::Minus: {
                advance();
                // Unary minus followed by number
                if (tok_.kind == Tk::Number) {
                    auto node = std::make_unique<AstNode>();
                    node->kind = AstNode::Kind::Number;
                    node->is_float = tok_.num_is_float;
                    if (tok_.num_is_float) {
                        node->float_val = -tok_.num_f64;
                    } else {
                        node->int_val = -(int64_t)tok_.num_u64;
                        // For negative big int preserve prefix
                        if (tok_.is_big_int)
                            node->str_val = "-" + tok_.num_str;
                    }
                    advance();
                    return node;
                }
                error_ = "expected number after '-'";
                return nullptr;
            }
            case Tk::Rb: {
                // Empty struct/tuple -- return nullptr to signal end
                return nullptr;
            }
            case Tk::Rbk:
                error_ = "unexpected ']'";
                return nullptr;
            case Tk::Eof:
                return nullptr;
            default:
                error_ = std::string("unexpected token at line ") + std::to_string(0);
                return nullptr;
        }
    }

    // Parse .{ ... }  (struct or tuple)
    // Named struct: .{ .key = val, ... }
    // Tuple:        .{ val, val, ... }
    // Both close with just }
    
    
    std::unique_ptr<AstNode> parse_struct_or_tuple() {
        auto node = std::make_unique<AstNode>();
        bool has_named_fields = false;

        while (true) {
            if (tok_.kind == Tk::Rb) {
                advance();
                node->kind = has_named_fields ? AstNode::Kind::Struct : AstNode::Kind::Tuple;
                return node;
            }
            if (tok_.kind == Tk::Eof) {
                error_ = "unclosed struct/tuple";
                return nullptr;
            }

            if (tok_.kind == Tk::Period) {
                advance();
                if (tok_.kind == Tk::Identifier) {
                    std::string key(tok_.sv);
                    advance();
                    if (tok_.kind == Tk::Equal) {
                        advance();
                        auto val = parse_value();
                        if (!val) { error_ = "expected value after '.'" + key + "='"; return nullptr; }
                        if (!node->items.empty()) { error_ = "cannot mix fields and values in struct"; return nullptr; }
                        node->fields.emplace_back(key, std::move(val));
                        has_named_fields = true;
                        if (tok_.kind == Tk::Comma) advance();
                        continue;
                    }
                    auto en = std::make_unique<AstNode>();
                    en->kind = AstNode::Kind::EnumLit;
                    en->str_val = key;
                    node->items.push_back(std::move(en));
                    if (tok_.kind == Tk::Comma) advance();
                    continue;
                }
                if (tok_.kind == Tk::Lb) {
                    advance();
                    auto nested = parse_struct_or_tuple();
                    if (!nested) return nullptr;
                    node->items.push_back(std::move(nested));
                    if (tok_.kind == Tk::Comma) advance();
                    continue;
                }
                error_ = "unexpected token after '.'";
                return nullptr;
            }

            auto val = parse_value();
            if (!val) { error_ = "expected value or field"; return nullptr; }

            if (has_named_fields) { error_ = "cannot mix fields and values"; return nullptr; }
            node->items.push_back(std::move(val));
            if (tok_.kind == Tk::Comma) advance();
        }
    }
    std::unique_ptr<AstNode> parse_array() {
        auto node = std::make_unique<AstNode>();
        node->kind = AstNode::Kind::Array;

        while (tok_.kind != Tk::Rbk && tok_.kind != Tk::Eof) {
            auto val = parse_value();
            if (!val && tok_.kind != Tk::Rbk) {
                error_ = "expected value or ']'";
                return nullptr;
            }
            if (val) {
                node->items.push_back(std::move(val));
                if (tok_.kind == Tk::Comma) advance();
            }
        }

        if (tok_.kind != Tk::Rbk) {
            error_ = "expected ']'";
            return nullptr;
        }
        advance(); // consume ]
        return node;
    }
};

#endif // ZCP_PARSER_HPP
