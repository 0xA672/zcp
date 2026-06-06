#include "parser.hpp"
#include <cstdio>
#include <cmath>
#include <string>

static void print_node(const AstNode& node, int indent = 0) {
    auto is = [](int n) { return std::string(n * 2, ' '); };
    switch (node.kind) {
        case AstNode::Kind::Struct: {
            if (node.fields.empty()) { std::printf(".{}"); return; }
            std::printf(".{\n");
            for (size_t i = 0; i < node.fields.size(); i++) {
                auto& [k, v] = node.fields[i];
                std::printf("%s.%s = ", is(indent + 1).c_str(), k.c_str());
                print_node(*v, indent + 1);
                if (i + 1 < node.fields.size()) std::printf(",");
                std::printf("\n");
            }
            std::printf("%s}", is(indent).c_str());
            break;
        }
        case AstNode::Kind::Tuple: {
            if (node.items.empty()) { std::printf(".{}"); return; }
            std::printf(".{\n");
            for (size_t i = 0; i < node.items.size(); i++) {
                std::printf("%s", is(indent + 1).c_str());
                print_node(*node.items[i], indent + 1);
                if (i + 1 < node.items.size()) std::printf(",");
                std::printf("\n");
            }
            std::printf("%s}", is(indent).c_str());
            break;
        }
        case AstNode::Kind::Array: {
            if (node.items.empty()) { std::printf("[]"); return; }
            std::printf("[\n");
            for (size_t i = 0; i < node.items.size(); i++) {
                std::printf("%s", is(indent + 1).c_str());
                print_node(*node.items[i], indent + 1);
                if (i + 1 < node.items.size()) std::printf(",");
                std::printf("\n");
            }
            std::printf("%s]", is(indent).c_str());
            break;
        }
        case AstNode::Kind::String:  std::printf("\"%s\"", node.str_val.c_str()); break;
        case AstNode::Kind::Number:
            if (node.is_float) std::printf("%g", node.float_val);
            else if (!node.str_val.empty()) std::printf("%s", node.str_val.c_str());
            else std::printf("%llu", (unsigned long long)node.int_val);
            break;
        case AstNode::Kind::Bool:    std::printf(node.bool_val ? "true" : "false"); break;
        case AstNode::Kind::Null:    std::printf("null"); break;
        case AstNode::Kind::Nan:     std::printf("nan"); break;
        case AstNode::Kind::Inf:     std::printf("inf"); break;
        case AstNode::Kind::EnumLit: std::printf(".%s", node.str_val.c_str()); break;
        case AstNode::Kind::Identifier: std::printf("%s", node.str_val.c_str()); break;
        case AstNode::Kind::Char:    std::printf("'\\u{%x}'", node.cp); break;
        case AstNode::Kind::MultiLine: std::printf("\\\\%s", node.str_val.c_str()); break;
    }
}

static const AstNode* find_field(const AstNode& node, const std::string& name) {
    if (node.kind != AstNode::Kind::Struct) return nullptr;
    for (auto& [k, v] : node.fields) if (k == name) return v.get();
    return nullptr;
}

static std::string ss(const AstNode* n) {
    return (!n || n->kind != AstNode::Kind::String) ? "" : n->str_val;
}
static uint64_t si(const AstNode* n) {
    return (!n || n->kind != AstNode::Kind::Number || n->is_float) ? 0 : n->int_val;
}
static double sf(const AstNode* n) {
    return (!n || n->kind != AstNode::Kind::Number || !n->is_float) ? 0 : n->float_val;
}
static bool sb(const AstNode* n) {
    return n && n->kind == AstNode::Kind::Bool && n->bool_val;
}

struct Case { const char* name; const char* input; bool (*verify)(const AstNode&); };
static int pass = 0, total = 0;

#define CHECK(desc, cond) do { \
    bool _ok = (cond); \
    std::printf("  %s  %s\n", _ok ? "OK" : "FAIL", desc); \
    ok = ok && _ok; \
} while(0)

static bool v_zon_doc(const AstNode& r) {
    bool ok = true;
    CHECK("struct", r.kind == AstNode::Kind::Struct);
    CHECK("b==hello", ss(find_field(r, "b")) == "hello, world!");
    CHECK("a~=1.5", std::fabs(sf(find_field(r, "a")) - 1.5) < 0.001);
    auto* c = find_field(r, "c");
    CHECK("c tuple", c && c->kind == AstNode::Kind::Tuple && c->items.size() == 2);
    auto* e = find_field(r, "e");
    CHECK("e struct", e && e->kind == AstNode::Kind::Struct);
    if (e) { CHECK("e.x==13", si(find_field(*e, "x")) == 13); CHECK("e.y==67", si(find_field(*e, "y")) == 67); }
    return ok;
}

static bool v_simple(const AstNode& r) {
    bool ok = true;
    CHECK("struct", r.kind == AstNode::Kind::Struct);
    CHECK("name==zcp", ss(find_field(r, "name")) == "zcp");
    CHECK("ver==0.1.0", ss(find_field(r, "version")) == "0.1.0");
    CHECK("enabled", sb(find_field(r, "enabled")));
    CHECK("count==42", si(find_field(r, "count")) == 42);
    CHECK("null", find_field(r, "optional") && find_field(r, "optional")->kind == AstNode::Kind::Null);
    CHECK("pi~=3.14", std::fabs(sf(find_field(r, "pi")) - 3.14) < 0.001);
    CHECK("hex==255", si(find_field(r, "hex")) == 255);
    CHECK("bin==42", si(find_field(r, "bin")) == 42);
    CHECK("oct==493", si(find_field(r, "oct")) == 493);
    CHECK("big", find_field(r, "big") && find_field(r, "big")->kind == AstNode::Kind::Number && find_field(r, "big")->str_val == "18446744073709551616");
    return ok;
}

static bool v_nested(const AstNode& r) {
    bool ok = true;
    auto* m = find_field(r, "meta");
    CHECK("meta struct", m && m->kind == AstNode::Kind::Struct);
    if (m) { CHECK("license==MIT", ss(find_field(*m, "license")) == "MIT"); CHECK("count==3", si(find_field(*m, "count")) == 3); }
    auto* d = find_field(r, "deps");
    CHECK("deps tuple", d && d->kind == AstNode::Kind::Tuple && d->items.size() == 3);
    return ok;
}

static bool v_tuple(const AstNode& r) {
    bool ok = true;
    CHECK("tuple", r.kind == AstNode::Kind::Tuple && r.items.size() == 3);
    if (r.items.size() == 3) {
        CHECK("str", r.items[0]->kind == AstNode::Kind::String);
        CHECK("int", r.items[1]->kind == AstNode::Kind::Number && !r.items[1]->is_float && r.items[1]->int_val == 42);
        CHECK("nested tuple", r.items[2]->kind == AstNode::Kind::Tuple);
    }
    return ok;
}

static bool v_standalone(const AstNode&) { std::printf("  OK  parsed\n"); return true; }

static bool v_hex_float(const AstNode& r) {
    bool ok = std::fabs(r.float_val - 1.0) < 0.001;
    std::printf("  %s  0x1p0==1.0\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_big(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::Number && !r.is_float && r.str_val == "18446744073709551616";
    std::printf("  %s  big int\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_enum(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::Tuple && r.items.size() == 2;
    if (ok) {
        ok = r.items[0]->kind == AstNode::Kind::EnumLit && r.items[0]->str_val == "foo" &&
             r.items[1]->kind == AstNode::Kind::EnumLit && r.items[1]->str_val == "bar";
    }
    std::printf("  %s  enum lits .foo .bar\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_neg(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::Tuple && r.items.size() == 2;
    if (ok) {
        ok = r.items[0]->kind == AstNode::Kind::Number && !r.items[0]->is_float && r.items[0]->int_val == 42;
        ok = ok && r.items[1]->kind == AstNode::Kind::Number && r.items[1]->is_float && std::fabs(r.items[1]->float_val + 3.14) < 0.001;
    }
    std::printf("  %s  -42 and -3.14\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_char(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::Char && r.cp == 120;
    std::printf("  %s  'x'==120\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_multiline(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::MultiLine;
    std::printf("  %s  multiline\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_array(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::Array && r.items.size() == 3;
    if (ok) for (int i = 0; i < 3; i++) ok = ok && r.items[i]->kind == AstNode::Kind::Number && r.items[i]->int_val == (uint64_t)(i + 1);
    std::printf("  %s  [1,2,3]\n", ok ? "OK" : "FAIL"); return ok;
}

static bool v_inf(const AstNode& r) {
    bool ok = r.kind == AstNode::Kind::Inf;
    std::printf("  %s  inf\n", ok ? "OK" : "FAIL"); return ok;
}

Case tests[] = {
    {"ZON doc example", ".{\n.a = 1.5,\n.b = \"hello, world!\",\n.c = .{ true, false },\n.d = .{ 1, 2, 3 },\n.e = .{ .x = 13, .y = 67 },\n}", v_zon_doc},
    {"Simple struct", ".{\n.name = \"zcp\",\n.version = \"0.1.0\",\n.enabled = true,\n.count = 42,\n.optional = null,\n.pi = 3.14,\n.hex = 0xFF,\n.bin = 0b101010,\n.oct = 0o755,\n.big = 18446744073709551616,\n}", v_simple},
    {"Nested structs", ".{\n.meta = .{ .license = \"MIT\", .count = 3 },\n.deps = .{ \"a\", \"b\", \"c\" },\n}", v_nested},
    {"Mixed tuple", ".{ \"hello\", 42, .{ 1, 2, 3 } }", v_tuple},
    {"String", "\"hello\"", v_standalone},
    {"Int", "42", v_standalone},
    {"Float", "3.14", v_standalone},
    {"true", "true", v_standalone},
    {"false", "false", v_standalone},
    {"null", "null", v_standalone},
    {"nan", "nan", v_standalone},
    {"inf", "inf", v_inf},
    {"Hex float", "0x1p0", v_hex_float},
    {"Big int", "18446744073709551616", v_big},
    {"Enum lits", ".{ .foo, .bar }", v_enum},
    {"Negative nums", ".{ -42, -3.14 }", v_neg},
    {"Char", "'x'", v_char},
    {"Multiline", "\\\\hello\n\\\\world\n", v_multiline},
    {"Array", "[1, 2, 3]", v_array},
};

int main() {
    total = sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < total; i++) {
        std::printf("\n[%2d/%d] ====================\n", i + 1, total);
        std::printf("  %s\n", tests[i].name);
        std::printf("========================\n");
        Parser parser(tests[i].input);
        auto root = parser.parse();
        if (!root) {
            std::printf("  PARSE ERROR: %s\n", parser.error().c_str());
            continue;
        }
        std::printf("  AST: ");
        print_node(*root);
        std::printf("\n");
        if (tests[i].verify(*root)) pass++;
    }
    std::printf("\n========================================\n");
    std::printf("  %d/%d TESTS PASSED\n", pass, total);
    std::printf("========================================\n");
    return pass == total ? 0 : 1;
}
