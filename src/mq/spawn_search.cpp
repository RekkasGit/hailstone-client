#include "mq/spawn_search.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <glm/geometric.hpp>

#include "core/saturate.h"
#include "game/coords.h"
#include "game/eq_names.h"
#include "game/group.h"
#include "game/world_state.h"
#include "mq/tlo.h"

namespace hailstone::mq {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The words, a quoted run as one word.
std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;
        if (text[i] == '"') {
            std::size_t end = text.find('"', i + 1);
            out.emplace_back(text.substr(i + 1, end == std::string_view::npos ? std::string_view::npos : end - i - 1));
            i = end == std::string_view::npos ? text.size() : end + 1;
        } else {
            std::size_t end = text.find(' ', i);
            out.emplace_back(text.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
            i = end == std::string_view::npos ? text.size() : end;
        }
    }
    return out;
}

bool number(const std::string& w, double& out) { return parse_float(w, out); }

bool is_pet(const game::Spawn& s) { return s.pet_owner_id != 0; }

} // namespace

SpawnSearch parse_spawn_search(std::string_view text) {
    SpawnSearch s;
    auto w = words(text);
    std::vector<std::string> name_words;
    for (std::size_t i = 0; i < w.size(); ++i) {
        std::string k = lower(w[i]);
        auto next = [&](double& out) {
            if (i + 1 >= w.size() || !number(w[i + 1], out)) return false;
            ++i;
            return true;
        };
        double a = 0, b = 0;
        if (k == "pc") s.kind = SpawnSearch::Kind::Pc;
        else if (k == "npc") s.kind = SpawnSearch::Kind::Npc;
        else if (k == "corpse") s.kind = SpawnSearch::Kind::Corpse;
        else if (k == "npccorpse") s.kind = SpawnSearch::Kind::NpcCorpse;
        else if (k == "pccorpse") s.kind = SpawnSearch::Kind::PcCorpse;
        else if (k == "pet") s.kind = SpawnSearch::Kind::Pet;
        else if (k == "mercenary" || k == "merc") s.kind = SpawnSearch::Kind::Mercenary;
        else if (k == "any") s.kind = SpawnSearch::Kind::Any;
        else if (k == "id") {
            // An id no spawn can have (negative, past 32 bits, NaN) matches nothing.
            if (next(a)) {
                if (auto id = in_range<std::uint32_t>(a)) s.id = *id;
                else s.none = true;
            }
        } else if (k == "notid") {
            if (next(a))
                if (auto id = in_range<std::uint32_t>(a)) s.not_ids.push_back(*id);
        } else if (k == "radius") {
            if (next(a)) s.radius = in_range<float>(a).value_or(0.0f); // (NaN or huge: no limit)
        } else if (k == "zradius") {
            if (next(a)) s.zradius = in_range<float>(a).value_or(0.0f);
        } else if (k == "range") {
            if (next(a) && next(b)) {
                s.min_level = saturate_cast<int>(a);
                s.max_level = saturate_cast<int>(b);
            }
        } else if (k == "level") {
            if (next(a)) s.min_level = s.max_level = saturate_cast<int>(a);
        } else if (k == "class") {
            if (i + 1 < w.size()) s.class_name = w[++i];
        } else if (k == "race") {
            if (i + 1 < w.size()) s.race_name = w[++i];
        } else if (k == "loc") {
            // MQ's loc is "x y [z]" in its coordinates (the server's: game/coords.h).
            if (next(a) && next(b)) {
                double z = 0;
                next(z);
                const auto x = in_range<float>(a), y = in_range<float>(b), zz = in_range<float>(z);
                if (x && y && zz) s.loc = game::server_to_world({*x, *y, *zz});
                else s.none = true; // no place to measure from
            }
        } else if (k == "named") s.named = true;
        else if (k == "targetable") s.targetable = true;
        else if (k == "group") s.group = true;
        else if (k == "nopet") s.nopet = true;
        else if (k == "gm") s.gm = true;
        else if (k == "trader") s.trader = true;
        else if (k == "buyer") s.buyer = true;
        else if (k == "afk") s.afk = true;
        else if (k == "lfg") s.lfg = true;
        else if (k == "linkdead") s.linkdead = true;
        else if (k == "exact") s.exact = true;
        else if (k == "noalert" || k == "alert" || k == "nearalert" || k == "notnearalert") next(a); // alert lists: none here
        else if (k == "next" || k == "prev" || k == "los") {
            // accepted and ignored (no target cycling or line of sight in searches yet)
        } else if (!w[i].empty() && w[i][0] == '=') {
            s.exact = true;
            name_words.push_back(w[i].substr(1));
        } else {
            name_words.push_back(w[i]);
        }
    }
    for (std::size_t i = 0; i < name_words.size(); ++i) s.name += (i ? " " : "") + name_words[i];
    return s;
}

bool is_named(const game::Spawn& s) {
    if (!s.is_npc || s.is_corpse || is_pet(s) || s.mercenary) return false;
    std::string clean = game::display_name(s.name);
    if (clean.empty()) return false;
    if (clean[0] == '#') return true;
    std::string l = lower(clean);
    if (l.rfind("a ", 0) == 0 || l.rfind("an ", 0) == 0) return false;
    return std::isupper(static_cast<unsigned char>(clean[0])) != 0;
}

glm::vec3 spawn_position(const game::Spawn& s) { return s.display_valid ? s.display_position : s.position; }

std::vector<const game::Spawn*> search_spawns(const SpawnSearch& q, const game::WorldState& world,
                                              const game::GroupState* group) {
    if (q.none) return {};
    glm::vec3 origin{0.0f};
    if (q.loc) origin = *q.loc;
    else if (const game::Spawn* me = world.player()) origin = spawn_position(*me);
    std::vector<std::pair<float, const game::Spawn*>> found;
    for (const auto& [id, s] : world.spawns) {
        using K = SpawnSearch::Kind;
        bool corpse = s.is_corpse;
        switch (q.kind) {
        case K::Any: break;
        case K::Pc: if (s.is_npc || corpse) continue; break;
        case K::Npc: if (!s.is_npc || corpse || s.mercenary) continue; break;
        case K::Corpse: if (!corpse) continue; break;
        case K::NpcCorpse: if (!corpse || !s.is_npc) continue; break;
        case K::PcCorpse: if (!corpse || s.is_npc) continue; break;
        case K::Pet: if (!is_pet(s) || corpse) continue; break;
        case K::Mercenary: if (!s.mercenary) continue; break;
        }
        if (q.id && s.id != *q.id) continue;
        if (std::find(q.not_ids.begin(), q.not_ids.end(), s.id) != q.not_ids.end()) continue;
        if (s.level < q.min_level || s.level > q.max_level) continue;
        if (q.nopet && is_pet(s)) continue;
        if (q.named && !is_named(s)) continue;
        if (q.targetable && !s.targetable) continue;
        if (q.gm && !s.gm) continue;
        if (q.trader && !s.trader) continue;
        if (q.buyer && !s.buyer) continue;
        if (q.afk && !s.afk) continue;
        if (q.lfg && !s.lfg) continue;
        if (q.linkdead && !s.linkdead) continue;
        if (!q.class_name.empty() && !iequals(q.class_name, game::class_name(s.class_id)) &&
            !iequals(q.class_name, game::class_short_name(s.class_id)))
            continue;
        if (!q.race_name.empty() && !iequals(q.race_name, game::race_name(s.race))) continue;
        if (!q.name.empty()) {
            std::string clean = game::display_name(s.name);
            bool ok = q.exact ? (iequals(q.name, clean) || iequals(q.name, s.name))
                              : (icontains(clean, q.name) || icontains(s.name, q.name));
            if (!ok) continue;
        }
        if (q.group) {
            bool in = s.id == world.player_id || (group && !s.is_npc && group->has_member(s.name));
            if (!in) continue;
        }
        glm::vec3 p = spawn_position(s);
        float d = std::hypot(p.x - origin.x, p.y - origin.y);
        if (q.radius > 0 && d > q.radius) continue;
        if (q.zradius > 0 && std::abs(p.z - origin.z) > q.zradius) continue;
        found.emplace_back(d, &s);
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : a.second->id < b.second->id;
    });
    std::vector<const game::Spawn*> out;
    out.reserve(found.size());
    for (auto& f : found) out.push_back(f.second);
    return out;
}

} // namespace hailstone::mq
