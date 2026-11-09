#include "mq/event_pattern.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "mq/tlo.h"

namespace hailstone::mq {

namespace {

// ECMAScript's '.' (as libstdc++ has it) matches anything but these.
bool line_break(char c) { return c == '\n' || c == '\r'; }

} // namespace

EventPattern::EventPattern(std::string_view pattern) : size_(pattern.size()) {
    auto literal = [&](char c) {
        if (tokens_.empty() || tokens_.back().kind != Token::Kind::Literal) tokens_.push_back({Token::Kind::Literal, {}});
        tokens_.back().text += c;
    };
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (c == '#') {
            const std::size_t close = pattern.find('#', i + 1);
            if (close != std::string_view::npos) {
                const std::string_view inner = pattern.substr(i + 1, close - i - 1);
                std::int64_t n = 0;
                if (inner == "*") {
                    tokens_.push_back({Token::Kind::Any, {}});
                    i = close;
                    continue;
                }
                if (parse_int(inner, n) && n > 0) {
                    tokens_.push_back({Token::Kind::Arg, {}});
                    order_.push_back(static_cast<int>(n));
                    i = close;
                    continue;
                }
            }
        }
        literal(c); // a '#' that starts no #*# or #n# is itself; the scan goes on after it
    }
}

namespace {

// The rows of match()'s table, kept from call to call: each script's events are matched against every chat line,
// and a fresh table each time was an allocation per event per line. Rarely bigger than a few KB; one that grew past
// kKeepBytes (a long pattern matched on a long line) is let go after its call.
constexpr std::size_t kKeepBytes = std::size_t{1} << 20;
thread_local std::vector<unsigned char> g_rows;

struct Rows {
    std::vector<unsigned char>& v = g_rows;
    unsigned char* get(std::size_t bytes) {
        if (v.size() < bytes) v.resize(bytes);
        return v.data();
    }
    ~Rows() {
        if (v.capacity() > kKeepBytes) std::vector<unsigned char>().swap(v);
    }
};

} // namespace

void EventPattern::fill_row(std::size_t i, std::string_view line, const unsigned char* next, unsigned char* row) const {
    const std::size_t n = line.size();
    const Token& k = tokens_[i];
    if (k.kind == Token::Kind::Literal) {
        const std::size_t len = k.text.size();
        std::fill(row, row + n + 1, static_cast<unsigned char>(0));
        for (std::size_t p = 0; p + len <= n; ++p)
            row[p] = next[p + len] && line.compare(p, len, k.text) == 0;
        return;
    }
    row[n] = next[n];
    for (std::size_t p = n; p-- > 0;) row[p] = next[p] || (!line_break(line[p]) && row[p + 1]);
}

bool EventPattern::may_match(std::string_view line) const {
    // Each literal must come in its order, the first at the start and the last at the end: most lines fail here for
    // most patterns, without the table.
    std::size_t pos = 0;
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        const Token& k = tokens_[i];
        if (k.kind != Token::Kind::Literal) continue;
        if (i == 0) {
            if (!line.starts_with(k.text)) return false;
            pos = k.text.size();
        } else {
            const std::size_t at = line.find(k.text, pos);
            if (at == std::string_view::npos) return false;
            pos = at + k.text.size();
        }
        if (i + 1 == tokens_.size() && !line.ends_with(k.text)) return false;
    }
    return true;
}

std::optional<std::vector<std::string>> EventPattern::match(std::string_view line) const {
    if (!may_match(line)) return std::nullopt;
    const std::size_t n = line.size(), t = tokens_.size(), w = n + 1;
    Rows rows;
    // Whether the whole line matches: two rows, each from the one after it (tokens i.. match line[p..] to its end).
    {
        unsigned char* next = rows.get(2 * w);
        unsigned char* row = next + w;
        std::fill(next, next + w, static_cast<unsigned char>(0));
        next[n] = 1;
        for (std::size_t i = t; i-- > 0;) {
            fill_row(i, line, next, row);
            std::swap(next, row);
        }
        if (!next[0]) return std::nullopt;
    }
    // It does: the whole table, fits[i * w + p], for the arguments.
    unsigned char* fits = rows.get((t + 1) * w);
    std::fill(fits + t * w, fits + (t + 1) * w, static_cast<unsigned char>(0));
    fits[t * w + n] = 1;
    for (std::size_t i = t; i-- > 0;) fill_row(i, line, &fits[(i + 1) * w], &fits[i * w]);
    // The regex's own choice at each wildcard (a backtracking search takes the first that leads to a match): #*# the
    // longest, #n# the shortest, of those after which the rest still matches.
    std::vector<std::string> args;
    args.reserve(order_.size());
    std::size_t p = 0;
    for (std::size_t i = 0; i < t; ++i) {
        const Token& k = tokens_[i];
        if (k.kind == Token::Kind::Literal) {
            p += k.text.size();
            continue;
        }
        const unsigned char* next = &fits[(i + 1) * w];
        std::size_t end = p;
        while (end < n && !line_break(line[end])) ++end;
        std::size_t q = p;
        if (k.kind == Token::Kind::Any) {
            q = end;
            while (!next[q]) --q;
        } else {
            while (!next[q]) ++q;
            args.emplace_back(line.substr(p, q - p));
        }
        p = q;
    }
    return args;
}

} // namespace hailstone::mq
