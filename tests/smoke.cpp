#include "lexer.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL [L%d]: %s\n", __LINE__, msg); return 1; } \
    else { std::printf("  OK  %s\n", msg); } \
} while(0)

static int test_period() {
    std::printf("\n=== period ===\n");
    Lexer lex(".");
    Token t = lex.next();
    CHECK(t.kind == Tk::Period, "period");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_braces() {
    std::printf("\n=== braces ===\n");
    Lexer lex("{}[]");
    Token t = lex.next(); CHECK(t.kind == Tk::Lb, "{");
    t = lex.next(); CHECK(t.kind == Tk::Rb, "}");
    t = lex.next(); CHECK(t.kind == Tk::Lbk, "[");
    t = lex.next(); CHECK(t.kind == Tk::Rbk, "]");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_equal_comma() {
    std::printf("\n=== equal/comma ===\n");
    Lexer lex("=,");
    Token t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_minus() {
    std::printf("\n=== minus ===\n");
    Lexer lex("-");
    Token t = lex.next(); CHECK(t.kind == Tk::Minus, "-");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_identifier() {
    std::printf("\n=== identifier ===\n");
    Lexer lex("foo _bar baz123");
    Token t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "foo", "foo");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "_bar", "_bar");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "baz123", "baz123");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_keywords_are_identifiers() {
    std::printf("\n=== keywords are identifiers ===\n");
    Lexer lex("true false null nan inf");
    Token t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "true", "true");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "false", "false");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "null", "null");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "nan", "nan");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "inf", "inf");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_enum_literal() {
    std::printf("\n=== enum literal (.foo = period + ident) ===\n");
    Lexer lex(".foo .bar_baz");
    Token t = lex.next(); CHECK(t.kind == Tk::Period, ". before foo");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "foo", "foo");
    t = lex.next(); CHECK(t.kind == Tk::Period, ". before bar_baz");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "bar_baz", "bar_baz");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_numbers() {
    std::printf("\n=== numbers ===\n");
    struct { const char* in; uint64_t exp; } ints[] = {
        {"0",0},{"42",42},{"1_000_000",1000000},{"0xFF",255},{"0xABCD",43981},
        {"0b1010",10},{"0b1111_0000",240},{"0o77",63},{"0o1_000",512},
    };
    for (auto [in, exp] : ints) {
        Lexer lex(in); Token t = lex.next();
        CHECK(t.kind == Tk::Number && !t.num_is_float && t.num_u64 == exp, in);
        CHECK(lex.next().kind == Tk::Eof, "eof");
    }
    return 0;
}

static int test_leading_zero_rejected() {
    std::printf("\n=== leading zero rejected ===\n");
    try { Lexer("0123").next(); CHECK(false, "should throw"); }
    catch (...) { CHECK(true, "0123 rejected"); }
    try { Lexer("00").next(); CHECK(false, "should throw"); }
    catch (...) { CHECK(true, "00 rejected"); }
    return 0;
}

static int test_floats() {
    std::printf("\n=== floats ===\n");
    struct { const char* in; double exp; } cases[] = {
        {"3.14",3.14},{"0.5",0.5},{"1.",1.0},
        {"1e10",1e10},{"2.5e-3",0.0025},{"1.5e+2",150.0},
        {"0x1p0",1.0},{"0x1.8p4",24.0},{"0xap0",10.0},{"0x1p-1",0.5},
    };
    for (auto [in, exp] : cases) {
        Lexer lex(in); Token t = lex.next();
        CHECK(t.kind == Tk::Number && t.num_is_float && std::fabs(t.num_f64 - exp) < 0.001, in);
        CHECK(lex.next().kind == Tk::Eof, "eof");
    }
    return 0;
}

static int test_big_int() {
    std::printf("\n=== big integer ===\n");
    Lexer lex("18446744073709551616");
    Token t = lex.next();
    CHECK(t.kind == Tk::Number && !t.num_is_float && t.is_big_int && t.num_str == "18446744073709551616", "big int");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_string() {
    std::printf("\n=== string ===\n");
    Lexer lex("\"hello\" \"\"");
    Token t = lex.next(); CHECK(t.kind == Tk::String && t.str == "hello", "hello");
    t = lex.next(); CHECK(t.kind == Tk::String && t.str == "", "empty");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_string_escapes() {
    std::printf("\n=== string escapes ===\n");
    Lexer lex("\"\\n\\t\\\\\\\"\\0\"");
    Token t = lex.next();
    CHECK(t.kind == Tk::String, "escape str");
    CHECK(t.str.size() == 5, "size 5");
    CHECK(t.str[0] == '\n', "\\n");
    CHECK(t.str[1] == '\t', "\\t");
    CHECK(t.str[2] == '\\', "\\\\");
    CHECK(t.str[3] == '"',  "\\\"");
    CHECK(t.str[4] == '\0', "\\0");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_char() {
    std::printf("\n=== char ===\n");
    Lexer lex("'a' 'z' '0'");
    Token t = lex.next(); CHECK(t.kind == Tk::Char && t.cp == 97, "'a'");
    t = lex.next(); CHECK(t.kind == Tk::Char && t.cp == 122, "'z'");
    t = lex.next(); CHECK(t.kind == Tk::Char && t.cp == 48, "'0'");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_multiline() {
    std::printf("\n=== multiline string ===\n");
    Lexer lex("\\\\line one\n\\\\line two\n");
    Token t = lex.next();
    CHECK(t.kind == Tk::MultiLine && t.str.find("line one") != std::string::npos, "line 1");
    t = lex.next();
    CHECK(t.kind == Tk::MultiLine && t.str.find("line two") != std::string::npos, "line 2");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_negative_number() {
    std::printf("\n=== negative number (-42) ===\n");
    Lexer lex("-42");
    Token t = lex.next(); CHECK(t.kind == Tk::Minus, "-");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 42, "42");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_comments() {
    std::printf("\n=== comments ===\n");
    Lexer l1("42 // comment\ntrue");
    Token t = l1.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 42, "line comment int");
    t = l1.next(); CHECK(t.kind == Tk::Identifier && t.sv == "true", "line comment ident");

    Lexer l2("/* outer /* inner */ more */ false");
    t = l2.next(); CHECK(t.kind == Tk::Identifier && t.sv == "false", "nested block comment");

    try { while (Lexer("/* oops").next().kind != Tk::Eof) {} CHECK(false, ""); }
    catch (...) { CHECK(true, "unclosed block comment throws"); }
    return 0;
}

static int test_dot_lb() {
    std::printf("\n=== .{ ===\n");
    Lexer lex(".{");
    Token t = lex.next(); CHECK(t.kind == Tk::Period, ".{ period");
    t = lex.next(); CHECK(t.kind == Tk::Lb, ".{ lb");
    CHECK(lex.next().kind == Tk::Eof, "eof");
    return 0;
}

static int test_complex_zon() {
    std::printf("\n=== complex ZON struct ===\n");
    const char* src =
        ".{\n"
        ".name = \"zcp\",\n"
        ".ver = \"0.1.0\",\n"
        ".enabled = true,\n"
        ".count = 42,\n"
        ".pi = 3.14,\n"
        ".data = .{ 1, 2, 3 },\n"
        ".meta = .{ .x = 1, .y = 2 },\n"
        ".}\n";
    Lexer lex(src);
    Token t(Tk::Eof);
    // .{ }
    t = lex.next(); CHECK(t.kind == Tk::Period, "top .");
    t = lex.next(); CHECK(t.kind == Tk::Lb, "top {");
    // .name = "zcp",
    t = lex.next(); CHECK(t.kind == Tk::Period, ".name .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "name", ".name");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::String && t.str == "zcp", "\"zcp\"");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .ver = "0.1.0",
    t = lex.next(); CHECK(t.kind == Tk::Period, ".ver .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "ver", ".ver");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::String && t.str == "0.1.0", "\"0.1.0\"");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .enabled = true,
    t = lex.next(); CHECK(t.kind == Tk::Period, ".enabled .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "enabled", ".enabled");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "true", "true");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .count = 42,
    t = lex.next(); CHECK(t.kind == Tk::Period, ".count .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "count", ".count");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 42, "42");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .pi = 3.14,
    t = lex.next(); CHECK(t.kind == Tk::Period, ".pi .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "pi", ".pi");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_is_float, "3.14");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .data = .{ 1, 2, 3 },
    t = lex.next(); CHECK(t.kind == Tk::Period, ".data .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "data", ".data");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Period, "inner .");
    t = lex.next(); CHECK(t.kind == Tk::Lb, "inner {");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 1, "1");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 2, "2");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 3, "3");
    t = lex.next(); CHECK(t.kind == Tk::Rb, "}");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .meta = .{ .x = 1, .y = 2 },
    t = lex.next(); CHECK(t.kind == Tk::Period, ".meta .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "meta", ".meta");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Period, "meta inner .");
    t = lex.next(); CHECK(t.kind == Tk::Lb, "meta inner {");
    t = lex.next(); CHECK(t.kind == Tk::Period, ".x .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "x", ".x");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 1, "1");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    t = lex.next(); CHECK(t.kind == Tk::Period, ".y .");
    t = lex.next(); CHECK(t.kind == Tk::Identifier && t.sv == "y", ".y");
    t = lex.next(); CHECK(t.kind == Tk::Equal, "=");
    t = lex.next(); CHECK(t.kind == Tk::Number && t.num_u64 == 2, "2");
    t = lex.next(); CHECK(t.kind == Tk::Rb, "}");
    t = lex.next(); CHECK(t.kind == Tk::Comma, ",");
    // .}
    t = lex.next(); CHECK(t.kind == Tk::Period, "closing .");
    t = lex.next(); CHECK(t.kind == Tk::Rb, "closing }");
    CHECK(lex.next().kind == Tk::Eof, "EOF");
    return 0;
}

static int test_errors() {
    std::printf("\n=== error cases ===\n");
    try { Lexer("@").next(); CHECK(false, ""); } catch (...) { CHECK(true, "unexpected @"); }
    try { Lexer("\"unclosed").next(); CHECK(false, ""); } catch (...) { CHECK(true, "unclosed string"); }
    try { Lexer("'ab").next(); CHECK(false, ""); } catch (...) { CHECK(true, "unclosed char"); }
    try { Lexer("\\a").next(); CHECK(false, ""); } catch (...) { CHECK(true, "unexpected backslash"); }
    return 0;
}

int main() {
    struct { int (*fn)(); const char* name; } tests[] = {
        { test_period,          "period" },
        { test_braces,          "braces {}[]" },
        { test_equal_comma,     "equal/comma" },
        { test_minus,           "minus" },
        { test_identifier,      "identifier" },
        { test_keywords_are_identifiers, "keywords are identifiers" },
        { test_enum_literal,    "enum literal (.foo)" },
        { test_numbers,         "numbers" },
        { test_leading_zero_rejected, "leading zero rejected" },
        { test_floats,          "floats" },
        { test_big_int,         "big integer" },
        { test_string,          "string" },
        { test_string_escapes,  "string escapes" },
        { test_char,            "char" },
        { test_multiline,       "multiline string" },
        { test_negative_number, "negative number (-42)" },
        { test_comments,        "comments" },
        { test_dot_lb,          ".{" },
        { test_complex_zon,     "complex ZON struct" },
        { test_errors,          "error cases" },
    };
    int failed = 0, n = sizeof(tests)/sizeof(tests[0]);
    for (int i = 0; i < n; i++) {
        std::printf("\n[%2d/%d] ====================\n", i+1, n);
        std::printf("  %s\n", tests[i].name);
        try { if (tests[i].fn()) { failed++; } }
        catch (const std::exception& e) {
            std::fprintf(stderr, "  >>> EXCEPTION: %s\n", e.what());
            failed++;
        }
    }
    std::printf("\n========================================\n");
    if (failed == 0) std::printf("  ALL %d SMOKE TESTS PASSED\n", n);
    else std::fprintf(stderr, "  %d TEST GROUP(S) FAILED\n", failed);
    std::printf("========================================\n");
    return failed;
}
