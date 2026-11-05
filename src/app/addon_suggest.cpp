#include "app/addon_suggest.h"

#include <algorithm>

#include "core/addon_source.h"

namespace nmsc::app {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool has(const std::vector<std::string>& list, std::string_view key) {
    return !key.empty() && std::find(list.begin(), list.end(), key) != list.end();
}

} // namespace

std::string addon_repo_key(std::string_view source) {
    auto s = parse_addon_link(source);
    return s ? lower(s->owner + "/" + s->repo) : std::string();
}

std::vector<AddonSuggestion> addon_suggestions(const AddonIndex* index, std::string_view login_host,
                                               const std::vector<std::string>& worlds,
                                               const std::vector<std::string>& server_suggests, const AddonsHad& had,
                                               const std::vector<std::string>& dismissed) {
    std::vector<std::string> names, sources;
    for (const auto& n : had.names) names.push_back(lower(n));
    for (const auto& s : had.sources) sources.push_back(addon_repo_key(s));
    std::vector<std::string> seen; // repo keys suggested already
    std::vector<AddonSuggestion> out;
    auto add = [&](AddonSuggestion s) {
        const std::string key = addon_repo_key(s.source);
        if (key.empty() || has(seen, key)) return;
        seen.push_back(key);
        if (has(dismissed, key) || has(sources, key) || has(names, lower(s.name))) return;
        out.push_back(std::move(s));
    };
    if (index)
        for (auto& m : addon_index_matches(*index, login_host, worlds))
            add({m.addon.name, m.addon.source, m.world, m.addon.key, false});
    for (const auto& link : server_suggests) {
        // One the index lists is held to the index's name and key, whoever suggests it.
        const AddonIndexAddon* listed = nullptr;
        if (index)
            for (const auto& a : index->addons)
                if (addon_repo_key(a.source) == addon_repo_key(link)) listed = &a;
        if (listed) add({listed->name, listed->source, {}, listed->key, true});
        else add({{}, link, {}, std::nullopt, true});
    }
    return out;
}

std::vector<std::string> parse_dismissed_suggestions(std::string_view text) {
    std::vector<std::string> out;
    while (!text.empty()) {
        const std::size_t bar = text.find('|');
        const std::string key = addon_repo_key(text.substr(0, bar));
        if (!key.empty() && !has(out, key)) out.push_back(key);
        text = bar == std::string_view::npos ? std::string_view() : text.substr(bar + 1);
    }
    return out;
}

std::string join_dismissed_suggestions(const std::vector<std::string>& keys) {
    std::string out;
    for (const auto& k : keys) out += (out.empty() ? "" : "|") + k;
    return out;
}

std::string addon_suggestion_text(const AddonSuggestion& s, std::string_view server) {
    const std::string where = s.world.empty() ? (server.empty() ? std::string("This server") : std::string(server))
                                              : s.world;
    const std::string what = s.name.empty() ? s.source : s.name + " (" + s.source + ")";
    if (s.from_server) return where + " suggests a Hailstone add-on: " + what + ".";
    return where + " has a Hailstone add-on for its own features: " + what + ".";
}

} // namespace nmsc::app
