#pragma once

// The guild window's personal notes (CGuildMgmtWnd::LoadPersonalNotes @0x685770, SavePersonalNotes @0x682380,
// SetPersonalNote @0x6859d0, sub_00682450): notes only this player sees, one per member name, kept in a file of
// lines "%08X %s %s" (a time, the name, the note). The original's file is userdata\GN_<name>_<server>.txt; this
// client keeps it in the character's folder (core/character_files.h guild_notes()).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::game {

struct GuildPersonalNote {
    std::uint32_t time = 0; // __time32 when set, Util::FastTime when a member's note was last looked up
    std::string name;       // at most 63 bytes
    std::string note;       // at most 255 bytes
};

class GuildPersonalNotes {
public:
    // LoadPersonalNotes: the lines as fgets(0x153) reads them, each parsed with sscanf("%X %s %[^\n"); three fields
    // or the line is skipped; the name cut to 63 bytes and the note to 255. A name already read (Utf8HashNoCase:
    // any case) is not added again, its time becomes `now_ticks`.
    void parse(std::string_view text, std::uint32_t now_ticks);
    // SavePersonalNotes: "%08X %s %s\n" for each note, in order.
    std::string format() const;
    // sub_00682450 (CGuildMgmtWnd::AddMember): the note of `name` (any case), its time set to `now_ticks`; nullptr
    // when there is none.
    const std::string* find(std::string_view name, std::uint32_t now_ticks);
    // The note of `name` without touching its time.
    const std::string* get(std::string_view name) const;
    // SetPersonalNote: a known name takes the note (its time kept); else a new entry stamped `now_time`.
    void set(std::string_view name, std::string_view note, std::uint32_t now_time);

    std::vector<GuildPersonalNote> entries;
};

} // namespace hailstone::game
