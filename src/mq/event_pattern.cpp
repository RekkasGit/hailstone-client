#include "mq/event_pattern.h"

#include <cstddef>
#include <cstdint>

#include "mq/tlo.h"

namespace hailstone::mq {

namespace {

// ECMAScript's '.' (as libstdc++ has it) matches anything but these.
bool line_break(char c) { return c == '\n' || c == '\r'; }

} // namespace

EventPattern::EventPattern(std::string_view pattern) {
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

std::optional<std::vector<std::string>> EventPattern::match(std::string_view line) const {
    const std::size_t n = line.size(), t = tokens_.size(), w = n + 1;
    // fits[i * w + p]: tokens i.. match line[p..] to its end. Filled from the end, each row in one pass.
    std::vector<unsigned char> fits((t + 1) * w, 0);
    fits[t * w + n] = 1;
    for (std::size_t i = t; i-- > 0;) {
        const Token& k = tokens_[i];
        unsigned char* row = &fits[i * w];
        const unsigned char* next = &fits[(i + 1) * w];
        if (k.kind == Token::Kind::Literal) {
            const std::size_t len = k.text.size();
            for (std::size_t p = 0; p + len <= n; ++p)
                row[p] = next[p + len] && line.compare(p, len, k.text) == 0;
            continue;
        }
        row[n] = next[n];
        for (std::size_t p = n; p-- > 0;) row[p] = next[p] || (!line_break(line[p]) && row[p + 1]);
    }
    if (!fits[0]) return std::nullopt;
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
