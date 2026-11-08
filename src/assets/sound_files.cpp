#include "assets/sound_files.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "core/byte_io.h"
#include "core/msvc_atoi.h"

namespace hailstone::assets {

namespace {

// Splits text into lines on '\n'; a final line without newline counts too.
template <typename Fn> void for_each_line(std::string_view text, Fn&& fn) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        fn(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
}

// StrRemoveChar for CR, LF and space, as the bank and mp3 index readers do.
std::string strip_cr_lf_space(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (char c : line)
        if (c != '\r' && c != '\n' && c != ' ') out.push_back(c);
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// The original's delimited field reader: the text up to the next delimiter,
// converted with atoi / atof (so a missing or non-numeric field is 0).
class Fields {
public:
    Fields(std::string_view text, char delim) : text_(text), delim_(delim) {}
    std::string_view next() {
        if (pos_ > text_.size()) return {};
        std::size_t end = text_.find(delim_, pos_);
        if (end == std::string_view::npos) end = text_.size();
        std::string_view f = text_.substr(pos_, end - pos_);
        pos_ = end + 1;
        return f;
    }
    // 32-bit atoi (core/msvc_atoi.h), not strtol: its long is 64 bits on Linux and macOS and 32 on Windows, so a
    // number past int32 came out differently on each.
    std::int32_t next_int() { return core::msvc_atoi(next()); }
    float next_float() {
        std::string s(next());
        return std::strtof(s.c_str(), nullptr);
    }

private:
    std::string_view text_;
    char delim_;
    std::size_t pos_ = 0;
};

} // namespace

std::vector<EffSoundRecord> parse_sounds_eff(std::span<const std::byte> data) {
    std::vector<EffSoundRecord> out;
    for (std::size_t at = 0; at + kEffSoundRecordSize <= data.size(); at += kEffSoundRecordSize) {
        ByteReader r(data.subspan(at, kEffSoundRecordSize));
        EffSoundRecord e;
        r.skip(16);
        e.position.x = r.f32();
        e.position.y = r.f32();
        e.position.z = r.f32();
        e.radius = r.f32();
        e.cooldown[0] = r.le<std::int32_t>();
        e.cooldown[1] = r.le<std::int32_t>();
        e.random_delay = r.le<std::int32_t>();
        e.unknown44 = r.le<std::int32_t>();
        e.sound[0] = r.le<std::int32_t>();
        e.sound[1] = r.le<std::int32_t>();
        e.kind[0] = r.le<std::uint8_t>();
        e.kind[1] = r.le<std::uint8_t>();
        r.skip(2);
        e.level[0] = r.le<std::int32_t>();
        e.level[1] = r.le<std::int32_t>();
        e.fade_out[0] = r.le<std::int32_t>();
        e.fade_out[1] = r.le<std::int32_t>();
        e.param[0] = r.le<std::int32_t>();
        e.param[1] = r.le<std::int32_t>();
        out.push_back(e);
    }
    return out;
}

std::map<std::int32_t, std::string> parse_sound_bank(std::string_view text) {
    std::map<std::int32_t, std::string> out;
    std::int32_t id = 1;
    for_each_line(text, [&](std::string_view raw) {
        std::string line = strip_cr_lf_space(raw);
        if (iequals(line, "EMIT")) {
            id = 1;
        } else if (iequals(line, "LOOP")) {
            id = kSoundBankLoopBase;
        } else if (!line.empty()) {
            if (line.find('.') == std::string::npos) line += ".wav";
            out[id] = line;
            ++id;
        }
    });
    return out;
}

std::unordered_map<std::int32_t, std::string> parse_sound_assets(std::string_view text) {
    std::unordered_map<std::int32_t, std::string> out;
    for_each_line(text, [&](std::string_view raw) {
        if (raw.empty() || !std::isdigit(static_cast<unsigned char>(raw.front()))) return;
        Fields f(raw, '^');
        std::int32_t id = f.next_int();
        std::string name(f.next());
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
        if (name.empty() || iequals(name, "Unknown")) return;
        out[id] = std::move(name);
    });
    return out;
}

std::unordered_map<std::string, std::string> parse_named_sound_assets(std::string_view text) {
    std::unordered_map<std::string, std::string> out;
    for_each_line(text, [&](std::string_view raw) {
        if (raw.empty() || std::isdigit(static_cast<unsigned char>(raw.front()))) return;
        Fields f(raw, '^');
        std::string name(f.next());
        std::string file(f.next());
        while (!file.empty() && (file.back() == '\r' || file.back() == ' ')) file.pop_back();
        if (name.empty() || file.empty()) return;
        out[std::move(name)] = std::move(file);
    });
    return out;
}

std::vector<std::string> parse_mp3_index(std::string_view text) {
    std::vector<std::string> out;
    for_each_line(text, [&](std::string_view raw) {
        std::string line = strip_cr_lf_space(raw);
        if (line.empty() || out.size() >= 100) return;
        if (line.size() > 31) line.resize(31);
        out.push_back(std::move(line));
    });
    return out;
}

std::vector<EmtRow> parse_emt(std::string_view text) {
    std::vector<EmtRow> out;
    for_each_line(text, [&](std::string_view raw) {
        // fgets keeps the newline; the original skips lines of 10 characters or fewer.
        if (raw.size() + 1 <= 11) return;
        std::string line(raw);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        Fields f(line, ',');
        EmtRow r;
        r.version = f.next_int();
        r.file = std::string(f.next());
        while (!r.file.empty() && r.file.front() == ' ') r.file.erase(r.file.begin());
        while (!r.file.empty() && r.file.back() == ' ') r.file.pop_back();
        r.environment = f.next_int();
        r.period = f.next_int();
        r.gain = f.next_float();
        r.fade_in = f.next_int();
        r.fade_out = f.next_int();
        r.loop_count = f.next_int();
        r.position.x = f.next_float();
        r.position.y = f.next_float();
        r.position.z = f.next_float();
        r.full_volume_distance = f.next_float();
        r.max_distance = f.next_float();
        r.random_distance = f.next_float();
        r.range = f.next_float();
        r.delay_min = f.next_int();
        r.delay_max = f.next_int();
        r.xmi_track = f.next_int();
        r.echo = f.next_float();
        if (r.version < 2) {
            r.user_disableable = true;
            r.flag29 = false;
            r.flag2a = false;
        } else {
            r.user_disableable = f.next_int() != 0;
            r.flag29 = f.next_int() != 0;
            r.flag2a = f.next_int() != 0;
        }
        out.push_back(std::move(r));
    });
    return out;
}

bool is_live_racedata_line(std::string_view line) {
    return std::count(line.begin(), line.end(), '^') < 85;
}

int live_racedata_gender(std::string_view line) {
    std::size_t caret = line.find('^');
    if (caret == std::string_view::npos) return 2;
    std::string_view g = line.substr(caret + 1, line.find('^', caret + 1) - caret - 1);
    return g == "0" ? 0 : g == "1" ? 1 : 2;
}

std::unordered_map<std::int32_t, RaceSoundIds> parse_race_sounds(std::string_view text) {
    std::unordered_map<std::int32_t, RaceSoundIds> out;
    for_each_line(text, [&](std::string_view raw) {
        if (raw.empty() || !std::isdigit(static_cast<unsigned char>(raw.front()))) return;
        Fields f(raw, '^');
        std::int32_t race = f.next_int();
        if (is_live_racedata_line(raw)) {
            int gender = live_racedata_gender(raw);
            f.next();
            RaceSoundIds& ids = out[race];
            for (std::size_t k = 0; k < 44; ++k) {
                std::int32_t id = f.next_int();
                if (gender != 1) ids.male[k] = id;
                if (gender != 0) ids.female[k] = id;
            }
            return;
        }
        RaceSoundIds ids;
        for (std::size_t k = 0; k < 18; ++k) ids.male[k] = f.next_int();
        for (std::size_t k = 0; k < 18; ++k) ids.female[k] = f.next_int();
        for (std::size_t k = 18; k < 44; ++k) ids.male[k] = f.next_int();
        for (std::size_t k = 18; k < 44; ++k) ids.female[k] = f.next_int();
        out[race] = ids;
    });
    return out;
}

std::vector<SpellEffectSounds> parse_spell_effect_sounds(std::span<const std::byte> data) {
    constexpr std::size_t kRecord = 268;
    std::vector<SpellEffectSounds> out;
    for (std::size_t at = 0; at + kRecord <= data.size(); at += kRecord) {
        ByteReader r(data.subspan(at, kRecord));
        SpellEffectSounds e;
        e.name = r.fixed_string(64);
        e.cast = r.le<std::int32_t>();
        r.seek(132);
        e.projectile = r.le<std::int32_t>();
        r.seek(200);
        e.impact = r.le<std::int32_t>();
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace hailstone::assets
