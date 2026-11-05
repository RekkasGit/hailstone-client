#pragma once

// Small key=value settings file. Lines starting with '#' or ';' are comments;
// keys keep their insertion order when saved. Values are stored as text.
// Two clients may run at once with the same hailstone.ini: when the file changed on disk since this Config read or
// wrote it, save() keeps the other program's keys and writes only the keys set or erased here over them.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nmsc {

class Config {
public:
    // Reads the file; returns false (and leaves the config empty) if it cannot be read.
    bool load(const std::filesystem::path& path);
    // Reads key=value lines from text (what load() does with the file's contents).
    void parse(std::string_view text);
    // Writes all keys, creating the parent directory, through a temporary file renamed over the old one (a crash
    // never leaves half a file). When `path` is the file last read or written and it changed since, the file's
    // keys are read first and only the ones set or erased since go over them; this config then holds the merged
    // keys. Returns false on failure.
    bool save(const std::filesystem::path& path);
    std::string to_string() const;

    std::optional<std::string> get(std::string_view key) const;
    std::string get_string(std::string_view key, std::string_view fallback = {}) const;
    int get_int(std::string_view key, int fallback) const;
    bool get_bool(std::string_view key, bool fallback) const;

    void set(std::string_view key, std::string_view value);
    void set_int(std::string_view key, int value) { set(key, std::to_string(value)); }
    void set_bool(std::string_view key, bool value) { set(key, value ? "1" : "0"); }
    void erase(std::string_view key);

    const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }

private:
    std::vector<std::pair<std::string, std::string>> entries_;
    std::vector<std::string> changed_;   // keys set (to a new value) or erased since the last load or save
    std::filesystem::path disk_path_;    // the file last read or written
    std::optional<std::string> disk_text_; // what it held then
};

// The settings file of the client: hailstone.ini in the data folder (platform::config_file()).
std::filesystem::path default_config_path();

} // namespace nmsc
