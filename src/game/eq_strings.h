#pragma once

// The client's string tables: eqstr_us.txt (message templates by id, used by
// OP_FormattedMessage and others) and dbstr_us.txt (names by id and type, e.g.
// AA names). Templates use %1..%9 for arguments, %T<n> likewise, and
// %B<n>(<type>) for "argument n looked up in dbstr under that type".

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace hailstone { class GameDir; }

namespace hailstone::game {

class EqStrings {
public:
    void load(const GameDir& game);
    bool loaded() const { return !templates_.empty(); }
    bool has(std::uint32_t id) const { return templates_.contains(id); }

    // The template with its arguments filled in; "[id] args..." when the id is unknown.
    std::string format(std::uint32_t id, std::span<const std::string> args) const;
    std::string db_string(std::uint32_t id, std::uint32_t type) const;
    // Adds or replaces a template (tests).
    void set(std::uint32_t id, std::string text) { templates_[id] = std::move(text); }

private:
    std::unordered_map<std::uint32_t, std::string> templates_;
    std::unordered_map<std::uint64_t, std::string> db_;
};

} // namespace hailstone::game
