#pragma once

// An mq.event pattern (MacroQuest's): #*# is any text, #n# (n > 0) the n-th argument passed to the callback, and
// everything else is literal; the whole line must match. Matched as the ECMAScript std::regex it once was (#*# a
// greedy .*, #n# a lazy (.*?), neither across a line break): the same lines match with the same arguments. It runs in
// time linear in the line for a given pattern, where the regex backtracked (about n^k steps for k arguments on a line
// that does not match, on the main thread, for every chat line).
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::mq {

class EventPattern {
public:
    explicit EventPattern(std::string_view pattern);

    // The numbers of the #n# in the pattern, in their order there.
    const std::vector<int>& order() const { return order_; }
    // The text of each #n#, in the pattern's order, when the whole line matches.
    std::optional<std::vector<std::string>> match(std::string_view line) const;

    // The pattern's text: its tokens' bytes as written (what a script's events are held to, mq/lua_script.h).
    std::size_t size() const { return size_; }

private:
    struct Token {
        enum class Kind { Literal, Any, Arg } kind = Kind::Literal;
        std::string text; // a literal's
    };
    // A quick test that fails for most lines a pattern cannot match: its literals in their order.
    bool may_match(std::string_view line) const;
    // Row i of the table from row i + 1 (`next`): whether tokens i.. match line[p..] to its end, for each p.
    void fill_row(std::size_t i, std::string_view line, const unsigned char* next, unsigned char* row) const;
    std::vector<Token> tokens_;
    std::vector<int> order_;
    std::size_t size_ = 0;
};

} // namespace hailstone::mq
