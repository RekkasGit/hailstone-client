#include "core/config.h"

#include <algorithm>
#include <charconv>

#include "core/atomic_file.h"
#include "platform/paths.h"

namespace nmsc {

namespace {
std::string_view trim(std::string_view s) {
    const char* ws = " \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}
} // namespace

bool Config::load(const std::filesystem::path& path) {
    entries_.clear();
    changed_.clear();
    disk_path_ = path;
    disk_text_ = read_text_file(path);
    if (!disk_text_) return false;
    parse(*disk_text_);
    changed_.clear();
    return true;
}

void Config::parse(std::string_view text) {
    entries_.clear();
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string_view l = trim(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        if (l.empty() || l.front() == '#' || l.front() == ';' || l.front() == '[') continue;
        std::size_t eq = l.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = trim(l.substr(0, eq));
        if (!key.empty()) set(key, trim(l.substr(eq + 1)));
    }
}

std::string Config::to_string() const {
    std::string out = "# Hailstone settings\n";
    for (const auto& [k, v] : entries_) out += k + '=' + v + '\n';
    return out;
}

bool Config::save(const std::filesystem::path& path) {
    if (path == disk_path_) {
        auto now = read_text_file(path);
        if (now && now != disk_text_) {
            // Another program wrote the file since: its keys, with this config's changes over them.
            Config merged;
            merged.parse(*now);
            for (const std::string& key : changed_) {
                if (auto v = get(key)) merged.set(key, *v);
                else merged.erase(key);
            }
            entries_ = std::move(merged.entries_);
        }
    }
    std::string text = to_string();
    if (!write_file_atomic(path, text)) return false;
    disk_path_ = path;
    disk_text_ = std::move(text);
    changed_.clear();
    return true;
}

std::optional<std::string> Config::get(std::string_view key) const {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const auto& e) { return e.first == key; });
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

std::string Config::get_string(std::string_view key, std::string_view fallback) const {
    auto v = get(key);
    return v ? *v : std::string(fallback);
}

int Config::get_int(std::string_view key, int fallback) const {
    auto v = get(key);
    if (!v) return fallback;
    int out = 0;
    auto [p, ec] = std::from_chars(v->data(), v->data() + v->size(), out);
    return (ec == std::errc() && p == v->data() + v->size()) ? out : fallback;
}

bool Config::get_bool(std::string_view key, bool fallback) const {
    auto v = get(key);
    if (!v) return fallback;
    if (*v == "1" || *v == "true" || *v == "yes" || *v == "on") return true;
    if (*v == "0" || *v == "false" || *v == "no" || *v == "off") return false;
    return fallback;
}

void Config::set(std::string_view key, std::string_view value) {
    // Newlines would break the file format.
    std::string v(value);
    std::replace(v.begin(), v.end(), '\n', ' ');
    std::replace(v.begin(), v.end(), '\r', ' ');
    auto changed = [&] {
        if (std::find(changed_.begin(), changed_.end(), key) == changed_.end()) changed_.emplace_back(key);
    };
    for (auto& e : entries_)
        if (e.first == key) {
            if (e.second != v) changed();
            e.second = std::move(v);
            return;
        }
    changed();
    entries_.emplace_back(std::string(key), std::move(v));
}

void Config::erase(std::string_view key) {
    if (std::erase_if(entries_, [&](const auto& e) { return e.first == key; }) > 0 &&
        std::find(changed_.begin(), changed_.end(), key) == changed_.end())
        changed_.emplace_back(key);
}

std::filesystem::path default_config_path() { return platform::config_file(); }

} // namespace nmsc
