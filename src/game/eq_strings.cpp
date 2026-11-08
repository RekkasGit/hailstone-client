#include "game/eq_strings.h"

#include <cctype>
#include <cstdlib>
#include <string_view>

#include "core/game_dir.h"

namespace hailstone::game {

namespace {

template <typename Fn> void for_each_line(const std::vector<std::byte>& data, Fn fn) {
    std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        fn(line);
        pos = end + 1;
    }
}

std::uint64_t db_key(std::uint32_t id, std::uint32_t type) { return (static_cast<std::uint64_t>(type) << 32) | id; }

} // namespace

void EqStrings::load(const GameDir& game) {
    if (auto data = game.read("eqstr_us.txt")) {
        for_each_line(*data, [&](std::string_view line) {
            std::size_t space = line.find(' ');
            if (space == std::string_view::npos || space == 0 || !std::isdigit(static_cast<unsigned char>(line[0]))) return;
            auto id = static_cast<std::uint32_t>(std::strtoul(std::string(line.substr(0, space)).c_str(), nullptr, 10));
            templates_[id] = std::string(line.substr(space + 1));
        });
    }
    if (auto data = game.read("dbstr_us.txt")) {
        for_each_line(*data, [&](std::string_view line) {
            // id^type^text^...
            std::size_t a = line.find('^');
            if (a == std::string_view::npos) return;
            std::size_t b = line.find('^', a + 1);
            if (b == std::string_view::npos) return;
            std::size_t c = line.find('^', b + 1);
            auto id = static_cast<std::uint32_t>(std::strtoul(std::string(line.substr(0, a)).c_str(), nullptr, 10));
            auto type = static_cast<std::uint32_t>(std::strtoul(std::string(line.substr(a + 1, b - a - 1)).c_str(), nullptr, 10));
            db_[db_key(id, type)] = std::string(line.substr(b + 1, c == std::string_view::npos ? std::string_view::npos : c - b - 1));
        });
    }
}

std::string EqStrings::db_string(std::uint32_t id, std::uint32_t type) const {
    auto it = db_.find(db_key(id, type));
    return it == db_.end() ? std::string() : it->second;
}

std::string EqStrings::format(std::uint32_t id, std::span<const std::string> args) const {
    auto it = templates_.find(id);
    if (it == templates_.end()) {
        std::string out = "[" + std::to_string(id) + "]";
        for (const auto& a : args) out += " " + a;
        return out;
    }
    auto arg = [&](std::size_t n) -> std::string { return n >= 1 && n <= args.size() ? args[n - 1] : std::string(); };
    // FormatEqStrTemplate @0x4e1540 (rof2decomp notes/ui-chat-commands.md
    // 4.3): %T<n> expands argument n as an eqstr id with the same arguments
    // (EQEmu's GENERIC_STRINGID_SAY 554 "%1 says '%T2'" for NPC SayString),
    // nested at most 10 deep.
    auto expand = [&](auto& self, const std::string& t, int depth) -> std::string {
        std::string out;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] != '%' || i + 1 >= t.size()) {
                out.push_back(t[i]);
                continue;
            }
            char k = t[i + 1];
            if (k == '%') { // "%%": one percent sign
                out.push_back('%');
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(k))) {
                out += arg(static_cast<std::size_t>(k - '0'));
                ++i;
            } else if ((k == 'T' || k == 'B') && i + 2 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 2]))) {
                std::size_t n = static_cast<std::size_t>(t[i + 2] - '0');
                i += 2;
                if (k == 'B' && i + 1 < t.size() && t[i + 1] == '(') {
                    std::size_t close = t.find(')', i + 1);
                    if (close != std::string::npos) {
                        auto type = static_cast<std::uint32_t>(std::strtoul(t.substr(i + 2, close - i - 2).c_str(), nullptr, 10));
                        auto value = static_cast<std::uint32_t>(std::strtoul(arg(n).c_str(), nullptr, 10));
                        std::string name = db_string(value, type);
                        out += name.empty() ? arg(n) : name;
                        i = close;
                        continue;
                    }
                }
                std::string a = arg(n);
                if (k == 'T' && depth < 10 && !a.empty() && std::isdigit(static_cast<unsigned char>(a[0]))) {
                    auto inner = templates_.find(static_cast<std::uint32_t>(std::strtoul(a.c_str(), nullptr, 10)));
                    if (inner != templates_.end()) {
                        out += self(self, inner->second, depth + 1);
                        continue;
                    }
                }
                out += a;
            } else {
                out.push_back('%');
            }
        }
        return out;
    };
    return expand(expand, it->second, 0);
}

} // namespace hailstone::game
