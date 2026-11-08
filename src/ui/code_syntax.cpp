#include "ui/code_syntax.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace hailstone::ui {

namespace {

constexpr std::string_view kKeywords[] = {"and",  "break", "do",    "else", "elseif", "end",   "false", "for",
                                          "function", "goto", "if", "in",   "local",  "nil",   "not",   "or",
                                          "repeat", "return", "then", "true", "until", "while"};
constexpr std::string_view kBuiltins[] = {
    "mq", "state", "button", "print", "printf", "require", "ImGui", "pairs", "ipairs", "tostring", "tonumber",
    "type", "error", "pcall", "select", "string", "table", "math", "os", "coroutine", "utf8", "setmetatable",
    "getmetatable", "rawget", "rawset", "next", "unpack", "assert"};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The level of a long bracket opening at `at` ("[[" 0, "[==[" 2), or -1.
int long_open(std::string_view t, std::size_t at) {
    if (at >= t.size() || t[at] != '[') return -1;
    std::size_t i = at + 1;
    while (i < t.size() && t[i] == '=') ++i;
    return i < t.size() && t[i] == '[' ? static_cast<int>(i - at - 1) : -1;
}

// Past the long bracket of `level` that closes after `from` (the text's end when it does not close).
std::size_t long_close(std::string_view t, std::size_t from, int level) {
    std::string close = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
    auto at = t.find(close, from);
    return at == std::string_view::npos ? t.size() : at + close.size();
}

} // namespace

std::vector<CodeSpan> lua_code_spans(std::string_view t) {
    std::vector<CodeSpan> out;
    std::size_t i = 0;
    char prev = 0; // the last character before this span that is not white space
    while (i < t.size()) {
        char c = t[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        CodeSpan s{i, i + 1, CodeToken::Symbol};
        if (c == '-' && i + 1 < t.size() && t[i + 1] == '-') {
            s.kind = CodeToken::Comment;
            if (int level = long_open(t, i + 2); level >= 0) {
                s.end = long_close(t, i + 4 + static_cast<std::size_t>(level), level);
            } else {
                auto nl = t.find('\n', i);
                s.end = nl == std::string_view::npos ? t.size() : nl;
            }
        } else if (int level = long_open(t, i); level >= 0) {
            s.kind = CodeToken::String;
            s.end = long_close(t, i + 2 + static_cast<std::size_t>(level), level);
        } else if (c == '"' || c == '\'') {
            s.kind = CodeToken::String;
            std::size_t j = i + 1;
            while (j < t.size() && t[j] != c && t[j] != '\n') j += t[j] == '\\' ? 2 : 1;
            s.end = std::min(t.size(), j + 1);
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '.' && i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))) {
            s.kind = CodeToken::Number;
            std::size_t j = i + 1;
            while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '.' ||
                                    ((t[j] == '-' || t[j] == '+') && (t[j - 1] == 'e' || t[j - 1] == 'E' ||
                                                                      t[j - 1] == 'p' || t[j - 1] == 'P'))))
                ++j;
            s.end = j;
        } else if (ident_start(c)) {
            std::size_t j = i + 1;
            while (j < t.size() && ident(t[j])) ++j;
            s.end = j;
            std::string_view word = t.substr(i, j - i);
            if (std::find(std::begin(kKeywords), std::end(kKeywords), word) != std::end(kKeywords))
                s.kind = CodeToken::Keyword;
            else if (prev == '.' || prev == ':')
                s.kind = CodeToken::Member;
            else if (std::find(std::begin(kBuiltins), std::end(kBuiltins), word) != std::end(kBuiltins))
                s.kind = CodeToken::Builtin;
            else
                s.kind = CodeToken::Name;
        } else if (c == '.') {
            while (s.end < t.size() && s.end - i < 3 && t[s.end] == '.') ++s.end; // .. and ...
        }
        prev = s.kind == CodeToken::Symbol && s.end - s.begin == 1 ? c : 0;
        out.push_back(s);
        i = s.end;
    }
    return out;
}

} // namespace hailstone::ui
