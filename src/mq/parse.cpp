#include "mq/parse.h"

namespace nmsc::mq {

namespace {

// Reads a name and an optional [index] from `expr` at `pos`; the index may hold brackets of its own.
bool read_segment(std::string_view expr, std::size_t& pos, std::string_view& name, std::string_view& index) {
    std::size_t start = pos;
    while (pos < expr.size() && expr[pos] != '.' && expr[pos] != '[') ++pos;
    name = expr.substr(start, pos - start);
    index = {};
    if (pos < expr.size() && expr[pos] == '[') {
        int depth = 0;
        std::size_t open = pos;
        for (; pos < expr.size(); ++pos) {
            if (expr[pos] == '[') ++depth;
            else if (expr[pos] == ']' && --depth == 0) break;
        }
        if (pos >= expr.size()) return false; // no closing bracket
        index = expr.substr(open + 1, pos - open - 1);
        ++pos;
    }
    if (pos < expr.size()) {
        if (expr[pos] != '.') return false;
        ++pos;
    }
    return !name.empty();
}

} // namespace

Value evaluate(const Context& ctx, std::string_view expr) {
    if (!ctx.registry) return Value::nil();
    std::size_t pos = 0;
    std::string_view name, index;
    if (!read_segment(expr, pos, name, index)) return Value::nil();
    Value v = ctx.registry->tlo(ctx, name, index);
    while (pos < expr.size() && !v.is_nil()) {
        if (!read_segment(expr, pos, name, index)) return Value::nil();
        v = ctx.registry->member(ctx, v, name, index);
    }
    return v;
}

namespace {

// Deeper nesting than this is left as typed (a line of "${${${..." cannot run the stack out).
constexpr int kMaxDepth = 64;

// Expands `s` from `pos`. Inside a ${ (depth > 0) it stops past that ${'s closing '}' and sets `closed`. Each value is
// put in the output and never read again: a TLO's text that holds "${" (a name, a title, an item's name: what the
// server or other players chose) stays text. MQ2's ParseMacroData scans the whole line again after each replacement,
// so there a value is parsed once more; not here, a deliberate difference (docs/plugins.md "${...}").
std::string expand(const Context& ctx, std::string_view s, std::size_t& pos, int depth, bool& closed) {
    std::string out;
    while (pos < s.size()) {
        if (s[pos] == '$' && pos + 1 < s.size() && s[pos + 1] == '{' && depth < kMaxDepth) {
            pos += 2;
            bool inner_closed = false;
            std::string expr = expand(ctx, s, pos, depth + 1, inner_closed);
            if (!inner_closed) { // an unclosed ${ stays as typed (what is inside it expanded)
                out += "${";
                out += expr;
                continue;
            }
            out += ctx.registry ? ctx.registry->text(ctx, evaluate(ctx, expr)) : "NULL";
            continue;
        }
        if (depth > 0 && s[pos] == '}') {
            ++pos;
            closed = true;
            return out;
        }
        out += s[pos++];
    }
    return out;
}

} // namespace

std::string parse_macro_data(const Context& ctx, std::string_view line) {
    // Innermost first: an index may hold another ${...}, expanded before the expression around it is evaluated.
    std::size_t pos = 0;
    bool closed = false;
    return expand(ctx, line, pos, 0, closed);
}

} // namespace nmsc::mq
