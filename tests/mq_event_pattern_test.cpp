// mq/event_pattern.h: mq.event's patterns, matched as the std::regex they were built into before (the reference
// below is that conversion), without the regex's backtracking.
#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <regex>
#include <string>
#include <vector>

#include "mq/event_pattern.h"
#include "mq/tlo.h"

using namespace hailstone;

namespace {

// The regex mq.event used: #*# as .*, #n# as (.*?), the rest escaped.
std::string reference_regex(std::string_view pattern, std::vector<int>& order) {
    std::string re;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c == '#') {
            std::size_t close = pattern.find('#', i + 1);
            if (close != std::string_view::npos) {
                std::string_view inner = pattern.substr(i + 1, close - i - 1);
                std::int64_t n = 0;
                if (inner == "*") {
                    re += ".*";
                    i = close;
                    continue;
                }
                if (mq::parse_int(inner, n) && n > 0) {
                    re += "(.*?)";
                    order.push_back(static_cast<int>(n));
                    i = close;
                    continue;
                }
            }
        }
        if (std::string_view(".^$|()[]{}*+?\\/").find(c) != std::string_view::npos) re += '\\';
        re += c;
    }
    return re;
}

std::optional<std::vector<std::string>> reference_match(const std::string& pattern, const std::string& line,
                                                        std::vector<int>& order) {
    std::regex re(reference_regex(pattern, order));
    std::smatch m;
    if (!std::regex_match(line, m, re)) return std::nullopt;
    std::vector<std::string> out;
    for (std::size_t g = 1; g < m.size(); ++g) out.push_back(m[g].str());
    return out;
}

using Args = std::vector<std::string>;

} // namespace

TEST_CASE("mq.event patterns: MQ's wildcards, numbered arguments and literal text") {
    mq::EventPattern slain("#*#You have slain #1#!#*#");
    CHECK(slain.order() == std::vector<int>{1});
    CHECK(slain.match("You have slain a rat!") == Args{"a rat"});
    CHECK(slain.match("[x] You have slain a rat! Well done!") == Args{"a rat"}); // #1# the shortest: to the first '!'
    CHECK_FALSE(slain.match("You have slain a rat"));

    mq::EventPattern hits("#2# hits #1# for #3# points.");
    CHECK(hits.order() == std::vector<int>{2, 1, 3});
    CHECK(hits.match("A gnoll hits you for 12 points.") == Args{"A gnoll", "you", "12"});
    CHECK(hits.match("A hits B hits C for 1 points.") == Args{"A", "B hits C", "1"});
    CHECK_FALSE(hits.match("A gnoll hits you for 12 points"));

    mq::EventPattern greedy("#*# #1#");
    CHECK(greedy.match("a b c") == Args{"c"}); // #*# the longest
    mq::EventPattern lazy("#1# #2#");
    CHECK(lazy.match("a b c") == Args{"a", "b c"});

    // Not a #*# or #n#: the '#' is literal, and the scan goes on after it.
    mq::EventPattern hash("#a# #1#");
    CHECK(hash.order() == std::vector<int>{1});
    CHECK(hash.match("#a# x y") == Args{"x y"});
    CHECK(mq::EventPattern("#0#").match("#0#") == Args{});
    CHECK(mq::EventPattern("# 2 #").order() == std::vector<int>{2}); // the number may have spaces round it
    CHECK(mq::EventPattern("(1+1) = [#1#] $.^").match("(1+1) = [2] $.^") == Args{"2"});
    CHECK(mq::EventPattern("").match("") == Args{});
    CHECK_FALSE(mq::EventPattern("").match("x"));
    CHECK(mq::EventPattern("#*#").match("") == Args{});

    // As the regex's '.', a wildcard does not take a line break.
    CHECK_FALSE(mq::EventPattern("#1#").match("a\nb"));
    CHECK_FALSE(mq::EventPattern("#*#").match("a\rb"));
    CHECK(mq::EventPattern("#1#\n#2#").match("a\nb") == Args{"a", "b"});
}

TEST_CASE("mq.event patterns: the same matches and arguments as the regex, on random patterns and lines") {
    std::mt19937 rng(20261105);
    const std::vector<std::string> parts{"#*#", "#1#", "#2#", "#3#", "a", "b", " ", "!", "#", "#x#", "(", "\n", "."};
    const std::string alphabet = "ab !#(.\n";
    int matched = 0;
    for (int round = 0; round < 4000; ++round) {
        std::string pattern;
        for (int k = static_cast<int>(rng() % 6); k > 0; --k) pattern += parts[rng() % parts.size()];
        std::string line;
        for (int k = static_cast<int>(rng() % 10); k > 0; --k) line += alphabet[rng() % alphabet.size()];
        std::vector<int> order;
        const auto want = reference_match(pattern, line, order);
        mq::EventPattern p(pattern);
        INFO("pattern '" << pattern << "' line '" << line << "'");
        CHECK(p.order() == order);
        CHECK(p.match(line) == want);
        matched += want.has_value();
    }
    CHECK(matched > 100); // the cases matched often enough to compare arguments
}

TEST_CASE("mq.event patterns: many arguments on a long line that does not match take no time") {
    // The regex needed about C(n, 4) steps here (and could overflow its stack); this is a pass per part.
    mq::EventPattern p("#1# #2# #3# #4# #5#!");
    const std::string spaces(4000, ' ');
    CHECK_FALSE(p.match(spaces));
    CHECK(p.match(spaces + "!"));
}
