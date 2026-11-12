#pragma once

// A Windows-style ini file with [Sections] of key=value lines, as the original client writes its per-character
// file (<name>_<server>.ini: [HotButtons], [Socials], [Abilities] ...). Sections and keys keep their order; keys
// compare case-insensitively within a section, as GetPrivateProfileString does.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hailstone {

class IniFile {
public:
    bool load(const std::filesystem::path& path); // false (and empty) when the file cannot be read
    bool save(const std::filesystem::path& path) const;
    void parse(std::string_view text);
    std::string to_string() const;

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string get_string(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    int get_int(std::string_view section, std::string_view key, int fallback) const;
    // The keys of a section in file order (empty when there is no such section).
    std::vector<std::string> keys(std::string_view section) const;
    // The section names in file order.
    std::vector<std::string> sections() const;
    void set(std::string_view section, std::string_view key, std::string_view value);
    void set_int(std::string_view section, std::string_view key, int value) { set(section, key, std::to_string(value)); }
    void erase(std::string_view section, std::string_view key);
    // The section and all its keys.
    void erase_section(std::string_view section);
    // A key as the file keeps it: a line break or '=' becomes '_', and so does a leading ';', '#' or '[' (get, set
    // and erase clean keys this way).
    static std::string clean_key(std::string_view key);

private:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> entries;
    };
    Section* find(std::string_view name);
    const Section* find(std::string_view name) const;
    std::vector<Section> sections_;
};

// GetEQClientIniBool @0x5fa680 on a value: missing gives the default; true when it starts with T, t or 1 (an empty
// one is false). UseLuclinElementals is read this way with default TRUE.
bool eqclient_ini_bool(const std::optional<std::string>& text, bool fallback);

} // namespace hailstone
